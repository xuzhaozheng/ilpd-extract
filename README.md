# ilpd-extract

[English](README.md)｜[简体中文](README.zh.md)


ILPD (Immersive Lens Profile Data) is a file format used in the Apple Immersive Video production system to describe the imaging characteristics of immersive cameras and lenses. It is essentially a JSON file with the .ilpd extension.

ILPD files are typically provided by immersive camera manufacturers and may be embedded in original camera files (such as .braw). Extracted ILPD files can be used to generate AIME files or STMap.

## BRAW to ILPD Extractor


A command-line tool for extracting ILPD from Blackmagic RAW files created with URSA Cine Immersive. Currently, only the macOS version is available.

### macOS Dependencies
- Blackmagic RAW SDK for macOS, version 5.0 or above
- Xcode Command Line Tools or Xcode
- CMake 3.10 or above
- CoreFoundation framework

### How to Run

Before you start, please make sure you have downloaded and installed the correct version of the Blackmagic RAW SDK. Download it from the [Blackmagic Developer Website](https://www.blackmagicdesign.com/developer/products/braw/sdk-and-software).

For macOS, the SDK is located at `/Applications/Blackmagic RAW/Blackmagic RAW SDK`.

Then choose one of the following methods:
- **Use the pre-built version in the [Release](https://github.com/xuzhaozheng/ilpd-extract/releases/latest)**
    - macOS: Copy `Blackmagic RAW SDK/Mac/Libraries/BlackmagicRawAPI.framework` next to the `braw2ilpd` executable.

- **Build the binary yourself**: You can customize the SDK location and specify it in `CMakeLists.txt`. The current `CMakeLists.txt` assumes the `Blackmagic RAW SDK` folder is in the project root.

### Basic Usage

```bash
./braw2ilpd <input.braw> <output.ilpd>
```
Or use the `-a` or `--all` option to extract all immersive attributes to an additional .txt file:

```bash
./braw2ilpd <input.braw> <output.ilpd> -a
# or
./braw2ilpd <input.braw> <output.ilpd> --all
```

### Parameters

- `<input.braw>`: Path to the input Blackmagic RAW immersive video file
- `<output.ilpd>`: Path to save the extracted ILPD file
- `-a, --all`: Optional parameter to also generate a detailed immersive attributes txt file

### Supported Attributes

The `*_detailed_attributes.txt` file contains the following BRAW immersive video attributes:

| Attribute | Description |
|-----------|-------------|
| OpticalLensProcessingDataFileUUID | UUID of the ILPD file |
| OpticalILPDFileName | ILPD file name in camera |
| OpticalInteraxial | Lens optical axis distance |
| OpticalProjectionKind | Projection type (for Apple Immersive Video, 'fish') |
| OpticalCalibrationType | Calibration type (for ILPD lens projection, 'meiRives') |
| OpticalProjectionData | Actual ILPD data content |

## Development

### 1. Clone/Download the Project
```bash
git clone <repository-url>
cd ilpd-extract
```
### 2. Download Blackmagic RAW SDK

Download the `Blackmagic RAW SDK` and make sure `BRAW_SDK_PATH` in `CMakeLists.txt` points to the SDK location.

### 3. Build with CMake

```bash
mkdir build
cd build
cmake ..
make
```

The executable `braw2ilpd` will be generated in the build directory.

## License

This project uses the Blackmagic RAW SDK. For license information, please refer to the SDK license files in `Blackmagic RAW SDK/Documents/`.


## Known Issues

- For some braw files, the extracted `OpticalInteraxial` value is 0. This may be caused by a camera firmware bug or an issue with the current reading method.

## References

- Blackmagic RAW SDK Documentation
- [Apple Immersive Media Support/ImmersiveCameraLensDefinition](https://developer.apple.com/documentation/immersivemediasupport/immersivecameralensdefinition)
