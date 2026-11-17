#include "capture/capture_options.h"

#include <string>
#include <vector>

#include "test_util.h"

int main() {
    using capture::CaptureFlags;
    using capture::parseCaptureOptions;

    const capture::CaptureOptions none = parseCaptureOptions(CaptureFlags{});
    CHECK(!none.enabled);
    CHECK(none.error.empty());

    CaptureFlags listed;
    listed.capture = "0.2,1,3";
    const capture::CaptureOptions list_options = parseCaptureOptions(listed);
    CHECK(list_options.enabled);
    CHECK(list_options.error.empty());
    CHECK((list_options.at_times == std::vector<double>{0.2, 1, 3}));

    CaptureFlags unsorted;
    unsorted.capture = "3,1,0.2,1";
    const capture::CaptureOptions sorted = parseCaptureOptions(unsorted);
    CHECK((sorted.at_times == std::vector<double>{0.2, 1, 3}));

    CaptureFlags frame;
    frame.every = "frame";
    const capture::CaptureOptions every_frame = parseCaptureOptions(frame);
    CHECK(every_frame.enabled);
    CHECK(every_frame.every_frame);
    CHECK(every_frame.every_seconds == 0);

    CaptureFlags seconds;
    seconds.every = "0.5";
    const capture::CaptureOptions every_half = parseCaptureOptions(seconds);
    CHECK(every_half.enabled);
    CHECK(!every_half.every_frame);
    CHECK(every_half.every_seconds == 0.5);

    CaptureFlags bad_number;
    bad_number.capture = "0.2,abc";
    const capture::CaptureOptions bad = parseCaptureOptions(bad_number);
    CHECK(!bad.error.empty());
    CHECK(!bad.enabled);

    CaptureFlags negative_time;
    negative_time.capture = "-1";
    CHECK(!parseCaptureOptions(negative_time).error.empty());

    CaptureFlags zero_every;
    zero_every.every = "0";
    CHECK(!parseCaptureOptions(zero_every).error.empty());

    CaptureFlags backwards;
    backwards.capture = "0.2";
    backwards.from = "2";
    backwards.until = "1";
    const capture::CaptureOptions reversed = parseCaptureOptions(backwards);
    CHECK(!reversed.error.empty());
    CHECK(!reversed.enabled);

    CaptureFlags ranged;
    ranged.every = "1";
    ranged.from = "2";
    ranged.until = "5";
    const capture::CaptureOptions range_options = parseCaptureOptions(ranged);
    CHECK(range_options.error.empty());
    CHECK(range_options.from == 2);
    CHECK(range_options.until == 5);

    CaptureFlags orphan_from;
    orphan_from.from = "1";
    CHECK(!parseCaptureOptions(orphan_from).error.empty());
    CHECK(!parseCaptureOptions(orphan_from).enabled);

    CaptureFlags orphan_dump;
    orphan_dump.dump = true;
    CHECK(!parseCaptureOptions(orphan_dump).error.empty());

    CaptureFlags sized;
    sized.capture = "1";
    sized.size = "1280x720";
    const capture::CaptureOptions sized_options = parseCaptureOptions(sized);
    CHECK(sized_options.error.empty());
    CHECK(sized_options.width == 1280);
    CHECK(sized_options.height == 720);

    CaptureFlags zero_size;
    zero_size.capture = "1";
    zero_size.size = "0x10";
    CHECK(!parseCaptureOptions(zero_size).error.empty());

    CaptureFlags no_separator;
    no_separator.capture = "1";
    no_separator.size = "1280";
    CHECK(!parseCaptureOptions(no_separator).error.empty());

    CaptureFlags zero_limit;
    zero_limit.capture = "1";
    zero_limit.limit = "0";
    CHECK(!parseCaptureOptions(zero_limit).error.empty());

    CaptureFlags text_limit;
    text_limit.capture = "1";
    text_limit.limit = "abc";
    CHECK(!parseCaptureOptions(text_limit).error.empty());

    CaptureFlags fast;
    fast.capture = "1";
    fast.fps = "241";
    CHECK(!parseCaptureOptions(fast).error.empty());

    CaptureFlags fps_ok;
    fps_ok.capture = "1";
    fps_ok.fps = "120";
    CHECK(parseCaptureOptions(fps_ok).fps == 120);

    CaptureFlags defaults;
    defaults.capture = "1";
    const capture::CaptureOptions def = parseCaptureOptions(defaults);
    CHECK(def.limit == 100);
    CHECK(def.width == 1920);
    CHECK(def.height == 1080);
    CHECK(def.fps == 60);
    CHECK(def.from == 0);
    CHECK(def.until == -1);
    CHECK(def.dir.empty());

    CaptureFlags output;
    output.capture = "1";
    output.dir = "/out";
    output.dump = true;
    output.window = true;
    const capture::CaptureOptions out = parseCaptureOptions(output);
    CHECK(out.dir == "/out");
    CHECK(out.dump);
    CHECK(out.window);

    CaptureFlags shot;
    shot.screenshot = "shot.png";
    const capture::CaptureOptions plain = parseCaptureOptions(shot);
    CHECK(plain.error.empty());
    CHECK(plain.enabled);
    CHECK((plain.at_times == std::vector<double>{0}));
    CHECK(plain.limit == 1);
    CHECK(plain.dir == ".");
    CHECK(plain.file_name == "shot.png");
    CHECK(plain.width == 1920 && plain.height == 1080);

    CaptureFlags delayed_shot;
    delayed_shot.screenshot = "/a/b/shot.png";
    delayed_shot.screenshot_delay = "2.5";
    delayed_shot.size = "1280x720";
    const capture::CaptureOptions delayed = parseCaptureOptions(delayed_shot);
    CHECK(delayed.error.empty());
    CHECK((delayed.at_times == std::vector<double>{2.5}));
    CHECK(delayed.dir == "/a/b");
    CHECK(delayed.file_name == "shot.png");
    CHECK(delayed.width == 1280 && delayed.height == 720);

    CaptureFlags root_shot;
    root_shot.screenshot = "/shot.png";
    CHECK(parseCaptureOptions(root_shot).dir == "/");

    CaptureFlags no_name;
    no_name.screenshot = "/a/b/";
    CHECK(!parseCaptureOptions(no_name).error.empty());
    CHECK(!parseCaptureOptions(no_name).enabled);

    CaptureFlags negative_delay;
    negative_delay.screenshot = "shot.png";
    negative_delay.screenshot_delay = "-1";
    CHECK(!parseCaptureOptions(negative_delay).error.empty());

    CaptureFlags text_delay;
    text_delay.screenshot = "shot.png";
    text_delay.screenshot_delay = "soon";
    CHECK(!parseCaptureOptions(text_delay).error.empty());

    CaptureFlags orphan_delay;
    orphan_delay.screenshot_delay = "1";
    CHECK(!parseCaptureOptions(orphan_delay).error.empty());
    CHECK(!parseCaptureOptions(orphan_delay).enabled);

    CaptureFlags shot_and_capture;
    shot_and_capture.screenshot = "shot.png";
    shot_and_capture.capture = "1";
    CHECK(!parseCaptureOptions(shot_and_capture).error.empty());
    CHECK(!parseCaptureOptions(shot_and_capture).enabled);

    CaptureFlags shot_with_dir;
    shot_with_dir.screenshot = "shot.png";
    shot_with_dir.dir = "/out";
    CHECK(!parseCaptureOptions(shot_with_dir).error.empty());

    CaptureFlags capture_names_no_file;
    capture_names_no_file.capture = "1";
    CHECK(parseCaptureOptions(capture_names_no_file).file_name.empty());

    return test::finish("capture options");
}
