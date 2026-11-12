"""Verify production listener defaults and scheduled emission without live I/O."""
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import reticulum_lxmf_cli as cli


class ListenerScheduleTests(unittest.TestCase):
    def listen(self, arguments):
        clock = SimpleNamespace(now=0.0)
        events = []
        announcements = []
        delivery = SimpleNamespace(hash=bytes(16))
        router = SimpleNamespace(
            register_delivery_callback=lambda callback: None,
            announce=lambda destination: announcements.append((clock.now, destination)),
        )
        args = cli.build_parser().parse_args(['listen', *arguments])
        def sleep(seconds):
            clock.now += seconds
        with patch.object(cli, 'init_stack', return_value=(None, router, delivery)), \
             patch.object(cli, 'emit', side_effect=lambda args, name, **data: events.append((name, data))), \
             patch.object(cli.time, 'time', side_effect=lambda: clock.now), \
             patch.object(cli.time, 'sleep', side_effect=sleep):
            self.assertEqual(cli.command_listen(args), 0)
        return args, announcements, events

    def test_default_matches_four_hours(self):
        args, announcements, _ = self.listen(['--duration', '7201'])
        self.assertEqual(args.announce_interval, 14400)
        self.assertEqual([time for time, _ in announcements], [0])

    def test_default_periodic_boundary(self):
        _, announcements, events = self.listen(['--duration', '14401'])
        self.assertEqual([time for time, _ in announcements], [0, 14400])
        self.assertEqual(sum(name == 'announce' for name, _ in events), 1)

    def test_explicit_isolated_short_override(self):
        _, announcements, _ = self.listen(['--announce-interval', '2', '--duration', '5'])
        self.assertEqual([time for time, _ in announcements], [0, 2, 4])

    def test_zero_disables_periodic_only(self):
        _, announcements, events = self.listen(['--announce-interval', '0', '--duration', '28801'])
        self.assertEqual([time for time, _ in announcements], [0])
        self.assertFalse(any(name == 'announce' for name, _ in events))

    def test_other_commands_keep_their_controls(self):
        parser = cli.build_parser()
        args = parser.parse_args(['announce', '--count', '3', '--interval', '7'])
        self.assertEqual((args.count, args.interval), (3, 7))
        args = parser.parse_args(['send', '--to', '01' * 16, '--text', 'test'])
        self.assertEqual((args.path_timeout, args.wait), (20, 30))


if __name__ == '__main__':
    unittest.main()
