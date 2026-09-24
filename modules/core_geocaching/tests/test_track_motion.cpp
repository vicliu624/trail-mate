#include "gps/gpx/ostream_sink.h"
#include "gps/gpx/track_writer.h"
#include <cassert>
#include <fstream>
#include <sstream>
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ostringstream stream;
    gps::gpx::OstreamSink sink(stream);
    gps::gpx::TrackPointOptions options;
    options.legacy_extensions = false;
    options.has_elevation = true;
    options.elevation = 123.4;
    options.has_speed = true;
    options.speed_mps = 2.75;
    options.has_course = true;
    options.course_deg = 90.0;
    assert(gps::gpx::writeTrackPoint(sink, 30.25, 120.15, "2026-09-16T00:00:00Z", 8, 7, options));
    const auto text = stream.str();
    assert(text.find("<tm:speed>2.75</tm:speed>") != std::string::npos);
    assert(text.find("<tm:course>90.0</tm:course>") != std::string::npos);
    assert(text.find("<ele>123.4</ele>") != std::string::npos);
    std::ofstream output(argv[1], std::ios::binary);
    assert(output.good());
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" version=\"1.1\" creator=\"Trail Mate\"><trk><trkseg>\n";
    output << text << "</trkseg></trk></gpx>\n";
    output.flush();
    assert(output.good());
    stream.str("");
    options.has_speed = false;
    assert(gps::gpx::writeTrackPoint(sink, 30.25, 120.15, "", 0, 7, options));
    assert(stream.str().find("tm:speed") == std::string::npos);
    assert(stream.str().find("tm:course") != std::string::npos);
}
