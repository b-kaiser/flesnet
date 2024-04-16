#ifndef MSAWRITER_HPP
#define MSAWRITER_HPP

#include <boost/program_options.hpp>
#include <iostream>

/**
 * @struct bytesNumber
 * @brief A wrapper around a std::size_t needed by boost::program_options.
 *
 * This struct merely a wrapper around a std::size_t needed by the
 * boost::program_options library to validate human-readable input for
 * the maxBytesPerArchive option in the msaWriterOption. This follows an
 * example provided in the boost::program_options documentation (of e.g.
 * boost version 1.85).
 */
struct bytesNumber {
  std::size_t value;
  bytesNumber(std::size_t value) : value(value) {}
  bytesNumber() : value(0) {}

  // Define the conversion operators following std::size_t behaviour.
  operator bool() const { return value; }
  operator std::size_t() const { return value; }
};

// Defining the << operator is necessary to allow the
// boost::program_options library to set default values (because it
// uses this operator to print an options description).
std::ostream& operator<<(std::ostream& os, const struct bytesNumber& bN);

/**
 * @brief Validate user input for the maxBytesPerArchive option.
 *
 * This function is only used by the boost::program_options library to
 * validate user input for the maxBytesPerArchive option in the
 * msaWriterOptions struct. It is not intended to be used directly.
 */
void validate(boost::any& v,
              const std::vector<std::string>& values,
              bytesNumber*,
              int);

/**
 * @struct msaWriterOptions
 * @brief Options that will be used by an msaWriter.
 *
 */
typedef struct msaWriterOptions {
  bool dryRun;
  bool beVerbose;
  std::string prefix;
  // Technically, the OutputSequence base classes, if used, does enforce
  // both of the following options. So, setting one of them to a
  // non-zero value (as of the time of writing), but not the other, will
  // behave as if the other value was set to SIZE_MAX. Practically, this
  // is not going to change behaviour.
  //
  // TODO: Comment at some appropriate place that setting one of these
  // results in the output file(s) being named with a sequence number,
  // regardless of whether a single file not exceeding the limits is
  // sufficient.
  std::size_t maxItemsPerArchive; // zero means no limit
  bytesNumber maxBytesPerArchive; // zero means no limit

  // TODO: Currently, the OutputArchiveSequence classes do not properly
  // handle the limits (at least not the maxBytesPerArchive limit).
  bool useSequence() {
    static bool gaveWarning = false;
    if (!gaveWarning) {
      // TODO: Move this message somewhere else.
      std::cerr
          << "Warning: Currently, the OutputArchiveSequence"
             " classes do not properly handle the limits (at least not the"
             " maxBytesPerArchive limit; limits may be exceeded by the"
             " size of a micro slice.)"
          << std::endl;
      gaveWarning = true;
    }
    return maxItemsPerArchive || maxBytesPerArchive;
  }
} msaWriterOptions;

/**
 * @brief Returns the default options for an msaWriter.
 *
 * The default options are:
 * - dryRun     = false
 * - beVerbose  = false
 *
 * @return The default options for an msaWriter.
 */
msaWriterOptions defaultMsaWriterOptions();

/**
 * @brief Command line options exclusive to the msaWriter.
 *
 * Can be used to parse command line options for the msaWriter. These
 * options are necessarily exclusive to the msaWriter. Options shared
 * with other classes need to be handled separately. Currently, we have
 * the following options:
 *
 * Shared options:
 *  Boolean switches:
 *    --verbose, -v corresponds to beVerbose
 *
 *  Other options:
 *    None
 *
 * Exclusive options:
 *  Boolean switches:
 *    --dry-run, -d corresponds to dryRun
 *
 *  Other options:
 *    None
 *
 * @param options The msaWriterOptions object to be used for boolean
 * switches, whose values are taken as defaults. Other options need
 * to be extracted from the variables_map.
 *
 * @param hidden Whether to return hidden or regular options. Hidden options are
 * additional options that are not shown in the help message unless explicitly
 * requested by specifying `--help` together with the `--verbose` option.
 *
 * @return The command line options for the msaWriter. Can be combined
 * with other command line options and used to parse user input. The
 * resulting variables_map can be used to construct an msaWriterOptions
 * object.
 *
 * @note boost::program_options throws an exception if multiple
 * options_description objects which contain the same option are
 * combined. Hence, in case of future name clashes, the
 * options_description must be adjusted and the shared option
 * defined somewhere else.
 */
boost::program_options::options_description
getMsaWriterOptionsDescription(msaWriterOptions& options, bool hidden);

/**
 * @brief Writes non switch options from the variables_map to the
 * msaWriterOptions object.
 *
 * The msaWriterOptions object is constructed based on the options present
 * in the variables_map object. Boolean switches, both exclusive to msaWriter
 * and shared ones, are ignored.
 *
 * @param vm The variables_map object that contains the command line options.
 * @return The msaWriterOptions object based on the command line options.
 *
 * @note Shared option which are boolean switches need to be set manually.
 * Exclusive boolean switches are set automatically.
 */
void getNonSwitchMsaWriterOptions(
    [[maybe_unused]] // Remove this line if the parameter is used
    const boost::program_options::variables_map& vm,
    [[maybe_unused]] // Remove this line if the parameter is used
    msaWriterOptions& msaWriterOptions);

/**
 * @class msaWriter
 * @brief This class represents a writer for micro slice archives.
 *
 * The msaWriter class provides functionality to write MSA data to a
 * file or other output stream. For now, many default methods such as
 * copy constructor, copy assignment, move constructor, and move
 * assignment are deleted until they are needed (if ever). Similarly,
 * the class is marked as final to prevent inheritance (until needed).
 */
class msaWriter final {
public:
  /**
   * @brief Constructor for the msaWriter object using default options.
   *
   */
  msaWriter() : msaWriter(defaultMsaWriterOptions()){};

  /**
   * @brief Constructs an msaWriter object with the specified options.
   *
   * @param options The options to be used by the msaWriter.
   */
  msaWriter(const msaWriterOptions& msaWriterOptions);

  /**
   * @brief Destructor for the msaWriter object.
   */
  ~msaWriter() = default;

  // Delete copy constructor:
  msaWriter(const msaWriter& other) = delete;

  // Delete copy assignment:
  msaWriter& operator=(const msaWriter& other) = delete;

  // Delete move constructor:
  msaWriter(msaWriter&& other) = delete;

  // Delete move assignment:
  msaWriter& operator=(msaWriter&& other) = delete;

private:
  const msaWriterOptions options;
};

#endif // MSAWRITER_HPP
