"""Pages downloads retry transient HTTP errors without hiding permanent failures."""
import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import call, patch
import urllib.error
import urllib.request

scripts = Path(__file__).resolve().parents[1] / "scripts"
spec = importlib.util.spec_from_file_location("prepare_pages_site", scripts / "prepare_pages_site.py")
pages = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = pages
sys.path.insert(0, str(scripts))
try:
    spec.loader.exec_module(pages)
finally:
    sys.path.pop(0)


class PagesDownloadRetryTests(unittest.TestCase):
    def setUp(self):
        self.request = urllib.request.Request("https://example.invalid/asset.bin")

    def http_error(self, status):
        return urllib.error.HTTPError(
            self.request.full_url, status, "HTTP failure", {}, io.BytesIO(b"error")
        )

    def test_transient_http_failure_then_success(self):
        for status in (408, 429, 500, 502, 503, 504):
            with self.subTest(status=status):
                error = self.http_error(status)
                response = object()
                with patch.object(pages.urllib.request, "urlopen", side_effect=[error, response]) as opened, \
                        patch.object(pages.time, "sleep") as slept:
                    self.assertIs(response, pages.open_with_retries(self.request, timeout=7))
                    self.assertEqual([call(self.request, timeout=7)] * 2, opened.call_args_list)
                    slept.assert_called_once_with(1)
                self.assertTrue(error.fp.closed)

    def test_transient_http_retries_exhausted(self):
        errors = [self.http_error(500) for _ in range(3)]
        with patch.object(pages.urllib.request, "urlopen", side_effect=errors) as opened, \
                patch.object(pages.time, "sleep") as slept:
            with self.assertRaises(urllib.error.HTTPError) as caught:
                pages.open_with_retries(self.request)
            self.assertIs(errors[-1], caught.exception)
            self.assertEqual(3, opened.call_count)
            self.assertEqual([call(1), call(2)], slept.call_args_list)
        self.assertTrue(all(error.fp.closed for error in errors[:-1]))
        errors[-1].close()

    def test_permanent_http_errors_fail_immediately(self):
        for status in (400, 401, 403, 404, 501, 505):
            with self.subTest(status=status):
                error = self.http_error(status)
                with patch.object(pages.urllib.request, "urlopen", side_effect=error) as opened, \
                        patch.object(pages.time, "sleep") as slept:
                    with self.assertRaises(urllib.error.HTTPError) as caught:
                        pages.open_with_retries(self.request)
                    self.assertIs(error, caught.exception)
                    opened.assert_called_once_with(self.request, timeout=30)
                    slept.assert_not_called()
                error.close()

    def test_single_attempt_has_no_retry(self):
        error = self.http_error(503)
        with patch.object(pages.urllib.request, "urlopen", side_effect=error) as opened, \
                patch.object(pages.time, "sleep") as slept:
            with self.assertRaises(urllib.error.HTTPError):
                pages.open_with_retries(self.request, attempts=1)
            self.assertEqual(1, opened.call_count)
            slept.assert_not_called()
        error.close()

    def test_network_errors_keep_existing_retry_behavior(self):
        response = object()
        with patch.object(pages.urllib.request, "urlopen", side_effect=[urllib.error.URLError("reset"), response]) as opened, \
                patch.object(pages.time, "sleep") as slept:
            self.assertIs(response, pages.open_with_retries(self.request))
            self.assertEqual(2, opened.call_count)
            slept.assert_called_once_with(1)


if __name__ == "__main__":
    unittest.main()
