#include <cassert>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <vector>

#include <tbb/flow_graph.h>

using namespace tbb::flow;
using namespace std;

// The maximum value that must be stored in an image.

const size_t max_image_value = 255;

// The size for all images.

const size_t M = 256;
const size_t N = 256;

// Pixel positions are returned from a handful of functions.

using pixel_positions = vector<pair<size_t, size_t>>;

///
/// Create a random generator for integers.
///

std::random_device rd;
std::mt19937 rng(rd());
std::uniform_int_distribution<size_t> uni(0, max_image_value);

///
/// rnd_image class that couples data representation and functions specified in the task.
///

class rnd_image {
public:

    // Fill random image with random values (duh).

    rnd_image(size_t height, size_t width) : height_(height),
                                             width_(width),
                                             pixels_(height, vector<size_t>(width)),
                                             id_(instances_created++) {
        for (size_t i = 0; i < height_; ++i) {
            for (size_t k = 0; k < width_; ++k) {
                pixels_[i][k] = uni(rng);
            }
        }
    }

    // TBB wants this class to have a default constructor for some reason.

    rnd_image() : rnd_image(M, N) {}

    // Can't use reduce_values_ here because pixels' positions are memorized.

    pixel_positions pixels_with_value(size_t value) const {
        pixel_positions positions;
        for (size_t i = 0; i < height_; ++i) {
            for (size_t k = 0; k < width_; ++k) {
                if (pixels_[i][k] == value) {
                    positions.push_back(make_pair(i, k));
                }
            }
        }
        return positions;
    }

    pixel_positions pixels_with_min_value() const {
        return pixels_with_value(reduce_values_(max_image_value, [](size_t x, size_t y) {
            return min(x, y);
        }));
    }

    pixel_positions pixels_with_max_value() const {
        return pixels_with_value(reduce_values_(0, [](size_t x, size_t y) {
            return max(x, y);
        }));
    }

    rnd_image &invert() {
        for (size_t i = 0; i < height_; ++i) {
            for (size_t k = 0; k < width_; ++k) {
                pixels_[i][k] = max_image_value - pixels_[i][k];
            }
        }
        return *this;
    }

    float mean_value() const {
        return reduce_values_(0, [](size_t sum, size_t value) {
            return sum + value;
        }) / static_cast<float>(height_ * width_);
    }

    void println() const {
        int pixel_width = static_cast<int>(floor(log10(max_image_value))) + 1;
        for (size_t i = 0; i < height_; ++i) {
            for (size_t k = 0; k < width_; ++k) {
                cout << setw(pixel_width) << pixels_[i][k] << ' ';
            }
            cout << endl;
        }
    }

    rnd_image &highlight_positions(const pixel_positions &positions) {
        auto outline = [this](const pair<size_t, size_t> &pos) {
            long r = pos.first, c = pos.second;
            pixel_positions outline_positions;

            for (long i = r - 1; i <= r + 1; ++i) {
                for (long k = c - 1; k <= c + 1; ++k) {
                    if (i >= 0 && i < height_ && k >= 0 && k < width_ && (i != r || k != c)) {
                        outline_positions.push_back(make_pair(static_cast<size_t >(i), static_cast<size_t >(k)));
                    }
                }
            }
            return outline_positions;
        };

        for (auto &pos : positions) {
            for (auto &out_pos :outline(pos)) {
                pixels_[out_pos.first][out_pos.second] = max_image_value;
            }
        }
        return *this;
    }

    size_t id() const {
        return id_;
    }

private:
    size_t height_;
    size_t width_;
    size_t id_;
    vector<vector<size_t>> pixels_;

    static size_t instances_created;

    size_t reduce_values_(size_t start, function<size_t(size_t, size_t)> fn) const {
        size_t reduced_value = start;
        for (size_t i = 0; i < height_; ++i) {
            for (size_t k = 0; k < width_; ++k) {
                reduced_value = fn(reduced_value, pixels_[i][k]);
            }
        }
        return reduced_value;
    }
};

size_t rnd_image::instances_created = 0;

int main(int argc, char *argv[]) {

    // Program can't recover if it receives an odd number of args.

    assert((argc - 1) % 2 == 0);

    ///
    /// Parse program's arguments.
    ///

    map<string, string> args;

    for (size_t i = 0; i < (argc - 1) / 2; ++i) {
        args[argv[(i * 2) + 1]] = argv[(i * 2) + 1 + 1];
    }

    if (!args.count("-b")) {
        cout << "You didn't provide a brightness value to highlight. Use -b option to do so." << endl;
        return 1;
    }

    if (!args.count("-l")) {
        cout << "You didn't provide a limit for number of images going through the graph." << endl;
        cout << "Use -l option to do so." << endl;
        return 2;
    }

    size_t brightness = stoul(args["-b"]);
    size_t limit = stoul(args["-l"]);

    ofstream log_file;

    if (args.count("-f")) {
        log_file.open(args["-f"]);

        // Program can't recover if the log file can't be opened.

        if (!log_file.is_open()) {
            cout << "Failed to open the log file." << endl;
            return 3;
        }
    }

    cout << "Received arguments:" << endl;
    cout << " Brightness value: " << brightness << endl;
    cout << " Concurrency limit: " << limit << endl;
    cout << " Log file path: " << args["-f"] << endl;

    ///
    /// Construct a graph.
    ///

    graph g;

    // Total number of images to create and pass into the graph.

    size_t nb_images = 10;

    // Source node calls its body until false is returned, define it to generate nb_images images.
    // Make this instance not active until explicitly activated.

    source_node<rnd_image> source_nd(g, [&](rnd_image &image_to_send) {
        static size_t nb_calls = 0;

        if (nb_calls >= nb_images) {
            return false;
        }

        nb_calls += 1;
        image_to_send = rnd_image();
        return true;
    }, false);

    ///
    /// Create wrappers for the first batch of rnd_image methods.
    ///

    // To correctly join results of these wrappers later we need to attach the same key to corresponding results.

    using keyed_pixel_positions = pair<size_t, pixel_positions>;

    auto with_fn = [brightness](const rnd_image &image) {
        return make_pair(image.id(), image.pixels_with_value(brightness));
    };

    auto min_fn = [](const rnd_image &image) {
        return make_pair(image.id(), image.pixels_with_min_value());
    };

    auto max_fn = [](const rnd_image &image) {
        return make_pair(image.id(), image.pixels_with_max_value());
    };

    function_node<rnd_image, keyed_pixel_positions> with_nd(g, unlimited, with_fn);
    function_node<rnd_image, keyed_pixel_positions> min_nd(g, unlimited, min_fn);
    function_node<rnd_image, keyed_pixel_positions> max_nd(g, unlimited, max_fn);

    // Connect them to the source_nd through a limiter node that prevents new images to go into the graph until
    // some of the old ones go out of it.
    // Limiter node doesn't buffer so in order to not lose messages from the source while the graph is busy
    // add a buffer node.

    buffer_node<rnd_image> source_buffer_nd(g);
    limiter_node<rnd_image> limit_nd(g, limit);

    make_edge(source_nd, source_buffer_nd);
    make_edge(source_buffer_nd, limit_nd);

    make_edge(limit_nd, with_nd);
    make_edge(limit_nd, max_nd);
    make_edge(limit_nd, min_nd);

    // Create a join node that awaits for all three results (and the image) to perform a sequential operation.

    // NOTE: Tutorials contain buffers before join slots, but they don't seem to be necessary according to the docs.

    // NOTE: If at least one successor accepts the tuple, the head of each input port's queue is removed. Does
    // it mean that some slow successors don't get their tuples?

    // NOTE: Results are added to a map with a corresponding key. Join node continues when all results
    // corresponding to a particular key are ready. Otherwise results that originate from different images
    // can sometimes get mixed up together in join.

    auto img_key = [](const rnd_image &image) {
        return image.id();
    };

    auto pixel_pos_key = [](pair<size_t, pixel_positions> p) {
        return p.first;
    };

    using join_nd_input = tuple<rnd_image, keyed_pixel_positions, keyed_pixel_positions, keyed_pixel_positions>;

    join_node<join_nd_input, key_matching<size_t>> join_nd(g,
                                                           img_key,
                                                           pixel_pos_key,
                                                           pixel_pos_key,
                                                           pixel_pos_key);

    // Connect wrappers to the join_nd.

    make_edge(source_nd, input_port<0>(join_nd));
    make_edge(with_nd, input_port<1>(join_nd));
    make_edge(min_nd, input_port<2>(join_nd));
    make_edge(max_nd, input_port<3>(join_nd));

    ///
    /// Create wrappers for the second batch of rnd_image methods.
    ///

    auto highlight_fn = [](join_nd_input data) {
        rnd_image cp_image = get<0>(data);
        cp_image.highlight_positions(get<1>(data).second);
        cp_image.highlight_positions(get<2>(data).second);
        cp_image.highlight_positions(get<3>(data).second);
        return cp_image;
    };

    auto mean_fn = [](const rnd_image &image) {
        return image.mean_value();
    };

    auto invert_fn = [](const rnd_image &image) {
        rnd_image cp_image(image);
        cp_image.invert();
        return cp_image;
    };

    auto log_fn = [&log_file](float mean_value) {
        if (log_file.is_open()) {
            log_file << "Mean pixel value across the image is " << mean_value << endl;
        }
        return continue_msg();
    };

    // Function node broadcasts return value to all its successors.

    function_node<join_nd_input, rnd_image> highlight_nd(
            g, unlimited, highlight_fn);
    function_node<rnd_image, float> mean_nd(g, unlimited, mean_fn);
    function_node<rnd_image, rnd_image> invert_nd(g, unlimited, invert_fn);
    function_node<float, continue_msg> log_nd(g, 1, log_fn);

    // Connect everything that is left to connect including decrement for the limiter node.
    // This decrement must happen after everything is done, so introduce a final join node.

    make_edge(join_nd, highlight_nd);
    make_edge(highlight_nd, mean_nd);
    make_edge(highlight_nd, invert_nd);
    make_edge(mean_nd, log_nd);

    join_node<tuple<rnd_image, continue_msg>> final_join_nd(g);
    auto pre_decrement_fn = [](tuple<rnd_image, continue_msg> const &t) {
        return continue_msg();
    };
    function_node<tuple<rnd_image, continue_msg>, continue_msg> pre_decrement_nd(g, unlimited, pre_decrement_fn);

    make_edge(invert_nd, input_port<0>(final_join_nd));
    make_edge(log_nd, input_port<1>(final_join_nd));

    make_edge(final_join_nd, pre_decrement_nd);
    make_edge(pre_decrement_nd, limit_nd.decrement);

    ///
    /// Activate source and wait for completion.
    ///

    source_nd.activate();
    g.wait_for_all();

    return 0;
}
