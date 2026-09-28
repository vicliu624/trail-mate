# Published public directory

The map loads this directory even when its live Reticulum bridge is offline.
`index.json` includes the export time and current public summaries. Immutable
signed objects are fetched on demand and verified against their summaries before
displaying details or producing GPX. Archived heads are retained so archive
filters and later exports preserve the publisher's state.

This is a dated copy, not a live global inventory. An unavailable bridge must not
be presented as an empty live result. The worker can use live directories when
available and falls back to the published copy when a live query fails.

To refresh from an operator's directory database:

```sh
python tools/geocaching/export_public_directory.py --database /path/to/directory.sqlite --output site/geocaching/data/public
```

Run `node --test tests/site/geocaching-snapshot.test.mjs` and publish the resulting
files through the normal Pages deployment. The exporter opens SQLite read-only,
verifies each signed object, excludes conflicting heads, and exports only public
records. Never copy a service state directory, identity file or SQLite database
into the website. Automatic collection and deployment still require an available
directory source and an authorized scheduled deployment.
