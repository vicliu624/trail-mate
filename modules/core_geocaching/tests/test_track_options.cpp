#include "gps/gpx/track_writer.h"
#include "gps/gpx/ostream_sink.h"
#include <cassert>
#include <sstream>
int main()
{
    std::ostringstream stream; gps::gpx::OstreamSink sink(stream);
    gps::gpx::TrackPointOptions options;
    options.has_elevation = false; options.legacy_extensions = false;
    assert(gps::gpx::writeTrackPoint(sink,1,2,"",0,7,options));
    assert(stream.str().find("<ele>")==std::string::npos && stream.str().find("<extensions>")==std::string::npos);
    stream.str("");options.has_elevation=true;options.elevation=123.25;options.elevation_precision=2;
    assert(gps::gpx::writeTrackPoint(sink,1,2,"2026-09-16T00:00:00Z",0,7,options));
    assert(stream.str().find("<ele>123.25</ele>")!=std::string::npos && stream.str().find("<time>2026-09-16T00:00:00Z</time>")!=std::string::npos);
}
