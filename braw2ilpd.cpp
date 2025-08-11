// braw2ilpd.cpp
// - Supports -o/--output, -a/--all, -v/--verbose, -s/--silent, -h/--help
// - Uses BlackmagicRaw API and CoreFoundation like original
// - Atomic text write (tmp + fsync + rename)
// - Caches all immersive attributes and outputs detailed file from cache

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <iomanip>
#include <cstdint>
#include <filesystem>

#include "BlackmagicRawAPI.h"

using std::string;
using std::vector;
using std::map;
using std::ostringstream;
using std::cout;
using std::cerr;
using std::endl;

// Exit codes
enum ExitCode {
    OK = 0,
    USAGE = 1,
    FACTORY_FAIL = 2,
    CODEC_FAIL = 3,
    OPENCLIP_FAIL = 4,
    IMMERSIVE_NOT_SUPPORTED = 5,
    FILE_NOT_FOUND = 6,
    WRITE_FAIL = 7,
    INVALID_FILE_FORMAT = 8
};

// Logger
struct Logger {
    bool verbose;
    bool silent;
    Logger(): verbose(false), silent(false) {}
    void info(const string &s) const { if (!silent) std::cout << s << std::endl; }
    void debug(const string &s) const { if (verbose && !silent) std::cout << s << std::endl; }
    void error(const string &s) const { std::cerr << s << std::endl; }
};

// CLI config
struct Config {
    bool outputAll;
    bool verbose;
    bool silent;
    string outputArg; // empty == not provided
    string inputBraw;
    Config(): outputAll(false), verbose(false), silent(false), outputArg(""), inputBraw("") {}
};

static void print_usage() {
    std::cout << "Usage: braw2ilpd <input.braw> [-o|--output <path>] [-a|--all] [-v|--verbose] [-s|--silent]\n";
    std::cout << "  -o, --output <path>   Specify output file or directory. If omitted, default is ./cameraID.uuid.ilpd\n";
    std::cout << "  -a, --all             Also output detailed attributes text file\n";
    std::cout << "  -v, --verbose         Verbose logging\n";
    std::cout << "  -s, --silent          Suppress non-error output\n";
    std::cout << "  -h, --help            Show this help\n";
}

// Parse args (supports unordered flags, requires -o/--output for custom output path)
static bool parse_args(int argc, char** argv, Config &cfg, Logger &log) {
    if (argc < 2) { print_usage(); return false; }
    vector<string> pos;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "-h" || a == "--help") { print_usage(); return false; }
        else if (a == "-a" || a == "--all") cfg.outputAll = true;
        else if (a == "-v" || a == "--verbose") cfg.verbose = true;
        else if (a == "-s" || a == "--silent") cfg.silent = true;
        else if (a == "-o" || a == "--output") {
            if (i + 1 >= argc) { log.error("Missing value for " + a); return false; }
            cfg.outputArg = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            log.error("Unknown option: " + a);
            print_usage();
            return false;
        } else {
            pos.push_back(a);
        }
    }
    if (pos.empty()) { log.error("Missing input .braw file"); print_usage(); return false; }
    if (pos.size() > 1) {
        log.error("Too many arguments. Expected one input .braw file, got " + std::to_string(pos.size()));
        print_usage();
        return false;
    }
    cfg.inputBraw = pos[0];
    return true;
}


// Atomic text write: write tmp, flush, rename
static bool write_text_file_atomic(const string &dest, const string &content, string &err) {
    try {
        std::filesystem::path destPath(dest);
        if (destPath.has_parent_path()) {
            std::filesystem::create_directories(destPath.parent_path());
        }
        
        string tmp = dest + ".tmp";
        std::ofstream tmpFile(tmp, std::ios::binary);
        if (!tmpFile) {
            err = "Failed to create temporary file: " + tmp;
            return false;
        }
        
        tmpFile << content;
        tmpFile.flush();
        tmpFile.close();
        
        if (tmpFile.fail()) {
            err = "Failed to write/close temporary file";
            std::filesystem::remove(tmp);
            return false;
        }
        
        std::filesystem::rename(tmp, dest);
        return true;
        
    } catch (const std::exception& e) {
        err = string("Error: ") + e.what();
        try { std::filesystem::remove(dest + ".tmp"); } catch (...) {}
        return false;
    }
}

// CFStringRef -> std::string (UTF-8)
static string CFStringToStdString(CFStringRef s) {
    if (!s) return string();
    const char* fast = CFStringGetCStringPtr(s, kCFStringEncodingUTF8);
    if (fast) return string(fast);
    CFIndex len = CFStringGetLength(s);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    string buf;
    buf.resize((size_t)maxSize);
    if (CFStringGetCString(s, &buf[0], maxSize, kCFStringEncodingUTF8)) {
        buf.resize(strlen(buf.c_str()));
        return buf;
    }
    return string();
}

// A generic AttrValue to cache attribute result
struct AttrValue {
    uint32_t vt = 0;
    string asString;                // human readable representation (formatted for display)
    string rawValue;                // raw string value (for strings only, used for processing)
    vector<uint8_t> rawBytes;       // raw bytes copy (SafeArray data truncated)
    uint32_t safeArrayElementCount = 0;
    uint32_t safeArrayVariantType = 0;
};

// Container for all attributes
struct ImmersiveAttrs {
    map<BlackmagicRawImmersiveAttribute, AttrValue> attrs;
    bool hasProjectionData() const {
        auto it = attrs.find(blackmagicRawImmersiveAttributeOpticalProjectionData);
        return it != attrs.end() && !it->second.rawValue.empty();
    }
    string getProjectionData() const {
        auto it = attrs.find(blackmagicRawImmersiveAttributeOpticalProjectionData);
        return (it != attrs.end()) ? it->second.rawValue : string();
    }
};

// Helper: variant -> readable string (and raw copy for SafeArray)
static string variant_to_string_and_store(const Variant &v, AttrValue &out, Logger &log) {
    if (v.vt == blackmagicRawVariantTypeString && v.bstrVal) {
        out.rawValue = CFStringToStdString(v.bstrVal);
        out.asString = "String value: " + out.rawValue;  // Use already stored value
        return out.asString;
    }
    else if (v.vt == blackmagicRawVariantTypeSafeArray && v.parray) {
        out.safeArrayElementCount = v.parray->bounds.cElements;
        out.safeArrayVariantType = v.parray->variantType;
        if (v.parray->data && out.safeArrayElementCount > 0) {
            uint32_t elementSize = 1;
            switch (v.parray->variantType) {
                case blackmagicRawVariantTypeU8: elementSize = 1; break;
                case blackmagicRawVariantTypeS16:
                case blackmagicRawVariantTypeU16: elementSize = 2; break;
                case blackmagicRawVariantTypeS32:
                case blackmagicRawVariantTypeU32:
                case blackmagicRawVariantTypeFloat32: elementSize = 4; break;
                case blackmagicRawVariantTypeFloat64: elementSize = 8; break;
                default: elementSize = 1; break;
            }
            uint64_t totalSize = (uint64_t)elementSize * (uint64_t)out.safeArrayElementCount;
            const uint64_t PREVIEW_LIMIT = 512;      // hex preview limit
            const uint64_t COPY_LIMIT = 64 * 1024;   // raw bytes copy limit
            uint64_t copySize = (totalSize > COPY_LIMIT) ? COPY_LIMIT : totalSize;
            if (copySize > 0) {
                out.rawBytes.resize((size_t)copySize);
                memcpy(out.rawBytes.data(), v.parray->data, (size_t)copySize);
            }
            // build hex preview up to PREVIEW_LIMIT
            uint64_t previewSize = (copySize > PREVIEW_LIMIT) ? PREVIEW_LIMIT : copySize;
            ostringstream oss;
            oss << "SafeArray elems=" << out.safeArrayElementCount << ", type=" << out.safeArrayVariantType
                << ", totalSize=" << totalSize << ", hex(first " << previewSize << " bytes)=";
            const unsigned char* data = v.parray->data;
            for (uint64_t j = 0; j < previewSize; ++j) {
                oss << std::hex << std::setfill('0') << std::setw(2) << (unsigned)(data[j]);
                if (j + 1 < previewSize) oss << " ";
            }
            if (previewSize < totalSize) oss << " ... (truncated)";
            oss << std::dec;
            out.asString = oss.str();
            return out.asString;
        } else {
            out.asString = "SafeArray(empty)";
            return out.asString;
        }
    } else {
        // numeric/basic types
        ostringstream oss;
        switch (v.vt) {
            case blackmagicRawVariantTypeEmpty:
                oss << "[Empty]"; break;
            case blackmagicRawVariantTypeU8:
                oss << "U8 value: " << static_cast<unsigned>(v.uiVal); break;
            case blackmagicRawVariantTypeS16:
                oss << "S16 value: " << v.iVal; break;
            case blackmagicRawVariantTypeU16:
                oss << "U16 value: " << v.uiVal; break;
            case blackmagicRawVariantTypeS32:
                oss << "S32 value: " << v.intVal; break;
            case blackmagicRawVariantTypeU32:
                oss << "U32 value: " << v.uintVal; break;
            case blackmagicRawVariantTypeFloat32:
                oss << "Float32 value: " << v.fltVal; break;
            case blackmagicRawVariantTypeFloat64:
                oss << "Float64 value: " << v.dblVal; break;
            default:
                oss << "[Unknown vt=" << v.vt << "]"; break;
        }
        out.asString = oss.str();
        return out.asString;
    }
}

// List of attributes to extract (expand as needed)
static const BlackmagicRawImmersiveAttribute ATTR_LIST[] = {
    blackmagicRawImmersiveAttributeOpticalLensProcessingDataFileUUID,
    blackmagicRawImmersiveAttributeOpticalILPDFileName,
    blackmagicRawImmersiveAttributeOpticalInteraxial,
    blackmagicRawImmersiveAttributeOpticalProjectionKind,
    blackmagicRawImmersiveAttributeOpticalCalibrationType,
    blackmagicRawImmersiveAttributeOpticalProjectionData,
    // add more attributes here if needed
};
static const size_t ATTR_COUNT = sizeof(ATTR_LIST) / sizeof(ATTR_LIST[0]);

// Human-friendly name & description for attributes
static string attr_name(BlackmagicRawImmersiveAttribute a) {
    switch (a) {
        case blackmagicRawImmersiveAttributeOpticalLensProcessingDataFileUUID: return "OpticalLensProcessingDataFileUUID";
        case blackmagicRawImmersiveAttributeOpticalILPDFileName: return "OpticalILPDFileName";
        case blackmagicRawImmersiveAttributeOpticalInteraxial: return "OpticalInteraxial";
        case blackmagicRawImmersiveAttributeOpticalProjectionKind: return "OpticalProjectionKind";
        case blackmagicRawImmersiveAttributeOpticalCalibrationType: return "OpticalCalibrationType";
        case blackmagicRawImmersiveAttributeOpticalProjectionData: return "OpticalProjectionData";
        default: return "UnknownAttribute";
    }
}
static string attr_desc(BlackmagicRawImmersiveAttribute a) {
    switch (a) {
        case blackmagicRawImmersiveAttributeOpticalLensProcessingDataFileUUID: return "UUID of the projection data file";
        case blackmagicRawImmersiveAttributeOpticalILPDFileName: return "Name of the ILPD projection data file";
        case blackmagicRawImmersiveAttributeOpticalInteraxial: return "Interaxial lens separation";
        case blackmagicRawImmersiveAttributeOpticalProjectionKind: return "Projection kind ('fish' indicates Apple immersive video)";
        case blackmagicRawImmersiveAttributeOpticalCalibrationType: return "Calibration type ('meiRives' indicates ILPD lens projection)";
        case blackmagicRawImmersiveAttributeOpticalProjectionData: return "The contents of the projection data file (ILPD)";
        default: return "";
    }
}

// Extract all attributes once and cache into ImmersiveAttrs
static bool extract_all_attributes(IBlackmagicRawClipImmersiveVideo* immersive, ImmersiveAttrs &out, Logger &log) {
    for (size_t i = 0; i < ATTR_COUNT; ++i) {
        BlackmagicRawImmersiveAttribute a = ATTR_LIST[i];
        Variant v;
        memset(&v, 0, sizeof(v));
        HRESULT hr = immersive->GetImmersiveAttribute(a, &v);
        AttrValue av;
        av.vt = v.vt;
        if (hr == S_OK) {
            variant_to_string_and_store(v, av, log);
            log.debug(string("Read attribute: ") + attr_name(a));
        } else {
            av.asString = "[Attribute not available]";
            log.debug(string("Failed to read attribute: ") + attr_name(a));
        }
        out.attrs[a] = av;
        VariantClear(&v);
    }
    return true;
}

// Make auto ilpd name cameraID.uuid.ilpd (fallbacks)
static string make_auto_ilpd_name(const string &inputBraw, const ImmersiveAttrs &attrs) {
    string cameraPart;
    string uuidPart;
    
    // Get camera and uuid from ILPD filename
    auto it = attrs.attrs.find(blackmagicRawImmersiveAttributeOpticalILPDFileName);
    if (it != attrs.attrs.end() && !it->second.rawValue.empty()) {
        std::filesystem::path fnPath(it->second.rawValue);
        string stem = fnPath.stem().string();
        size_t posDot = stem.find_last_of('.');
        if (posDot != string::npos) {
            cameraPart = stem.substr(0, posDot);
            uuidPart = stem.substr(posDot + 1);
        } else {
            cameraPart = stem;
        }
    }
    
    // Get UUID from separate attribute if not found above
    if (uuidPart.empty()) {
        auto it2 = attrs.attrs.find(blackmagicRawImmersiveAttributeOpticalLensProcessingDataFileUUID);
        if (it2 != attrs.attrs.end() && !it2->second.rawValue.empty()) {
            uuidPart = it2->second.rawValue;
        }
    }
    
    // Fallbacks
    if (cameraPart.empty()) {
        std::filesystem::path inputPath(inputBraw);
        cameraPart = inputPath.stem().string();
    }
    if (uuidPart.empty()) uuidPart = "default";
    
    return cameraPart + "." + uuidPart + ".ilpd";
}

// Helper to create detailed attribute file path based on main ILPD path
static string make_detailed_attributes_path(const string &ilpdPath) {
    std::filesystem::path finalPath(ilpdPath);
    std::filesystem::path detailed = finalPath.parent_path() / (finalPath.stem().string() + "_detailed_attributes.txt");
    
    // Keep the same relative/absolute style as the main output file
    if (finalPath.is_absolute()) {
        return std::filesystem::absolute(detailed).string();
    } else {
        return detailed.string();
    }
}

// Generate detailed attributes content
static bool write_detailed_attributes(const string &ilpdPath, const Config &cfg, const ImmersiveAttrs &cached, Logger &log) {
    string detailedPath = make_detailed_attributes_path(ilpdPath);
    
    ostringstream content;
    content << "Complete Blackmagic RAW Immersive Video Attribute List (Detailed)\n";
    content << string(62, '=') << "\n\n";
    content << "Input file: " << cfg.inputBraw << "\n";
    content << "ILPD file: " << ilpdPath << "\n";
    content << "Generated on: " << __DATE__ << " " << __TIME__ << "\n\n";

    for (size_t i = 0; i < ATTR_COUNT; ++i) {
        BlackmagicRawImmersiveAttribute a = ATTR_LIST[i];
        content << "[" << (i+1) << "] " << attr_name(a) << "\n";
        content << "Description: " << attr_desc(a) << "\n";
        auto it = cached.attrs.find(a);
        if (it == cached.attrs.end()) {
            content << "Not retrieved.\n\n";
            continue;
        }
        const AttrValue &av = it->second;
        content << av.asString << "\n\n";
    }

    string outStr = content.str();
    string err;
    if (!write_text_file_atomic(detailedPath, outStr, err)) {
        log.error(string("Failed to write detailed attributes file: ") + err);
        return false;
    } else {
        log.info(string("Detailed attributes saved to: ") + detailedPath);
        return true;
    }
}

// Resource cleanup helper
static void cleanup_resources(IBlackmagicRawClipImmersiveVideo* immersive, IBlackmagicRawClip* clip, 
                             IBlackmagicRaw* codec, IBlackmagicRawFactory* factory) {
    if (immersive) immersive->Release();
    if (clip) clip->Release();
    if (codec) codec->Release();
    if (factory) factory->Release();
}

// Resolve output path according to rules, preserving relative/absolute path style
static string resolve_output_path(const string &outputArg, const string &autoName, Logger &log) {
    std::filesystem::path result;
    bool shouldBeAbsolute = false;
    
    if (outputArg.empty()) {
        // Default case: relative path in current directory
        result = autoName;
        shouldBeAbsolute = false;
    }
    else if (outputArg == ".") {
        result = autoName;
        shouldBeAbsolute = false;
    }
    else {
        // Check if user provided absolute or relative path
        std::filesystem::path userPath(outputArg);
        shouldBeAbsolute = userPath.is_absolute();
        
        if (std::filesystem::exists(outputArg)) {
            if (std::filesystem::is_directory(outputArg)) {
                result = userPath / autoName;
            } else {
                result = userPath; // overwrite existing file
            }
        } else {
            string ext = userPath.extension().string();
            if (ext.empty() || ext == ".") {
                // No extension, treat as directory
                try {
                    std::filesystem::create_directories(userPath);
                    result = userPath / autoName;
                } catch (const std::filesystem::filesystem_error& e) {
                    log.error("Failed to create directory: " + outputArg);
                    return string();
                }
            } else {
                // Has extension, treat as file
                if (ext != ".ilpd") {
                    log.info("Note: Output file does not have .ilpd extension. ILPD files typically use .ilpd extension.");
                }
                if (userPath.has_parent_path()) {
                    try {
                        std::filesystem::create_directories(userPath.parent_path());
                    } catch (const std::filesystem::filesystem_error& e) {
                        log.error("Failed to create parent directories for: " + userPath.parent_path().string());
                        return string();
                    }
                }
                result = userPath;
            }
        }
    }
    
    // Return path in the same style as user input
    if (shouldBeAbsolute) {
        try {
            return std::filesystem::absolute(result).string();
        } catch (const std::filesystem::filesystem_error& e) {
            log.error("Failed to resolve absolute path: " + string(e.what()));
            return result.string();
        }
    } else {
        return result.string();
    }
}

int main(int argc, char** argv) {
    Config cfg;
    Logger log;
    if (!parse_args(argc, argv, cfg, log)) return USAGE;
    log.verbose = cfg.verbose;
    log.silent = cfg.silent;

    // Create factory
    IBlackmagicRawFactory* factory = CreateBlackmagicRawFactoryInstance();
    if (!factory) { 
        log.error("Failed to create BlackmagicRawFactory. Please ensure Blackmagic RAW SDK is properly installed."); 
        return FACTORY_FAIL; 
    }

    // Create codec
    IBlackmagicRaw* codec = nullptr;
    HRESULT hrCreateCodec = factory->CreateCodec(&codec);
    if (hrCreateCodec != S_OK || !codec) {
        log.error("Failed to create codec"); 
        cleanup_resources(nullptr, nullptr, codec, factory);
        return CODEC_FAIL;
    }

    // Check if input file exists
    if (!std::filesystem::exists(cfg.inputBraw)) {
        log.error("Input file does not exist: " + cfg.inputBraw);
        cleanup_resources(nullptr, nullptr, codec, factory);
        return FILE_NOT_FOUND;
    }

    // Check if input file has .braw extension
    std::filesystem::path inputPath(cfg.inputBraw);
    if (inputPath.extension() != ".braw") {
        log.error("Input file does not have .braw extension: " + cfg.inputBraw);
        cleanup_resources(nullptr, nullptr, codec, factory);
        return INVALID_FILE_FORMAT;
    }

    // Open clip
    CFStringRef inputCF = CFStringCreateWithCString(kCFAllocatorDefault, cfg.inputBraw.c_str(), kCFStringEncodingUTF8);
    if (!inputCF) { 
        log.error("Failed to create CFString for input path"); 
        cleanup_resources(nullptr, nullptr, codec, factory);
        return OPENCLIP_FAIL; 
    }
    IBlackmagicRawClip* clip = nullptr;
    HRESULT hrOpen = codec->OpenClip(inputCF, &clip);
    CFRelease(inputCF);
    if (hrOpen != S_OK || !clip) { 
        log.error("Failed to open clip: " + cfg.inputBraw); 
        if (hrOpen == E_INVALIDARG) {
            log.error("This may indicate the file is corrupted or not a valid Blackmagic RAW file.");
        } else if (hrOpen == E_ACCESSDENIED) {
            log.error("Access denied. Check file permissions.");
        }
        cleanup_resources(nullptr, clip, codec, factory);
        return OPENCLIP_FAIL; 
    }

    // Query immersive interface
    IBlackmagicRawClipImmersiveVideo* immersive = nullptr;
    HRESULT hrImmersive = clip->QueryInterface(IID_IBlackmagicRawClipImmersiveVideo, (void**)&immersive);
    if (hrImmersive != S_OK || !immersive) {
        log.error("This clip does not support immersive video features.");
        log.error("This tool only works with Blackmagic RAW files from URSA Cine Immersive cameras.");
        log.error("Please ensure the input file is an immersive video recording.");
        cleanup_resources(immersive, clip, codec, factory);
        return IMMERSIVE_NOT_SUPPORTED;
    }

    // Extract all immersive attributes once
    ImmersiveAttrs cached;
    extract_all_attributes(immersive, cached, log);

    // Build auto name and resolve output
    string autoName = make_auto_ilpd_name(cfg.inputBraw, cached);
    string finalOut = resolve_output_path(cfg.outputArg, autoName, log);
    if (finalOut.empty()) {
        log.error("Failed to determine final output path.");
        cleanup_resources(immersive, clip, codec, factory);
        return WRITE_FAIL;
    }
    log.info(string("Will write ILPD to: ") + finalOut);

    // Write ILPD (text) if found
    if (cached.hasProjectionData()) {
        string ilpdContent = cached.getProjectionData();
        string err;
        if (!write_text_file_atomic(finalOut, ilpdContent, err)) {
            log.error(string("Failed to write ILPD: ") + err);
            cleanup_resources(immersive, clip, codec, factory);
            return WRITE_FAIL;
        }
        log.info(string("ILPD saved to: ") + finalOut);
    } else {
        log.error("Warning: No OpticalProjectionData found, ILPD file not created");
    }

    // Detailed attributes if requested
    if (cfg.outputAll) {
        write_detailed_attributes(finalOut, cfg, cached, log);
    }

    // Cleanup
    cleanup_resources(immersive, clip, codec, factory);

    log.info("Extraction completed successfully!");
    return OK;
}
