#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <CoreFoundation/CoreFoundation.h>

#include "BlackmagicRawAPI.h"
int main(int argc, char** argv) {
    // Parse arguments
    bool outputAllAttributes = false;
    const char* inputBraw = nullptr;
    const char* outIlpd = nullptr;
    
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: braw2ilpd <input.braw> <output.ilpd> [-a|--all]\n";
        std::cerr << "Options:\n";
        std::cerr << "  -a, --all    Also output all attributes to a .txt file\n";
        return 1;
    }
    
    // Parse arguments
    if (argc == 3) {
        // Standard usage: braw2ilpd input.braw output.ilpd
        inputBraw = argv[1];
        outIlpd = argv[2];
    } else if (argc == 4) {
        // With option: braw2ilpd input.braw output.ilpd -a
        inputBraw = argv[1];
        outIlpd = argv[2];
        if (strcmp(argv[3], "-a") == 0 || strcmp(argv[3], "--all") == 0) {
            outputAllAttributes = true;
        } else {
            std::cerr << "Unknown option: " << argv[3] << "\n";
            std::cerr << "Usage: braw2ilpd <input.braw> <output.ilpd> [-a|--all]\n";
            std::cerr << "Options:\n";
            std::cerr << "  -a, --all    Also output all attributes to a .txt file\n";
            return 1;
        }
    }

    // Create factory
    IBlackmagicRawFactory* factory = CreateBlackmagicRawFactoryInstance();
    if (!factory) {
        std::cerr << "Failed to create BlackmagicRawFactory\n";
        return 2;
    }

    // Create codec
    IBlackmagicRaw* codec = nullptr;
    if (factory->CreateCodec(&codec) != S_OK || !codec) {
        std::cerr << "CreateCodec failed\n";
        factory->Release();
        return 3;
    }

    // Convert C string to CFStringRef
    CFStringRef inputBrawCF = CFStringCreateWithCString(kCFAllocatorDefault, inputBraw, kCFStringEncodingUTF8);
    if (!inputBrawCF) {
        std::cerr << "Failed to create CFStringRef\n";
        codec->Release();
        factory->Release();
        return 3;
    }

    // Open clip
    IBlackmagicRawClip* clip = nullptr;
    HRESULT openResult = codec->OpenClip(inputBrawCF, &clip);
    CFRelease(inputBrawCF); // Release CFStringRef
    
    if (openResult != S_OK || !clip) {
        std::cerr << "OpenClip failed: " << inputBraw << "\n";
        codec->Release();
        factory->Release();
        return 4;
    }

    // Get immersive interface
    IBlackmagicRawClipImmersiveVideo* immersive = nullptr;
    if (clip->QueryInterface(IID_IBlackmagicRawClipImmersiveVideo, (void**)&immersive) != S_OK || !immersive) {
        std::cerr << "This clip does not support IBlackmagicRawClipImmersiveVideo\n";
        clip->Release();
        codec->Release();
        factory->Release();
        return 5;
    }

    // Define all immersive attributes to extract
    struct ImmersiveAttribute {
        BlackmagicRawImmersiveAttribute attribute;
        const char* name;
        const char* description;
    };
    
    ImmersiveAttribute attributes[] = {
        {blackmagicRawImmersiveAttributeOpticalLensProcessingDataFileUUID, "OpticalLensProcessingDataFileUUID", "UUID of the projection data file"},
        {blackmagicRawImmersiveAttributeOpticalILPDFileName, "OpticalILPDFileName", "Name of the ILPD projection data file"},
        {blackmagicRawImmersiveAttributeOpticalInteraxial, "OpticalInteraxial", "Interaxial lens separation"},
        {blackmagicRawImmersiveAttributeOpticalProjectionKind, "OpticalProjectionKind", "Projection kind set to 'fish' to indicate Apple immersive video"},
        {blackmagicRawImmersiveAttributeOpticalCalibrationType, "OpticalCalibrationType", "Calibration type set to 'meiRives' to indicate ILPD lens projection"},
        {blackmagicRawImmersiveAttributeOpticalProjectionData, "OpticalProjectionData", "The contents of the projection data file"}
    };
    
    // First, extract the ILPD projection data (OpticalProjectionData)
    std::string ilpdContent;
    bool ilpdFound = false;
    
    // Extract ILPD content first
    for (size_t i = 0; i < sizeof(attributes) / sizeof(attributes[0]); i++) {
        if (attributes[i].attribute == blackmagicRawImmersiveAttributeOpticalProjectionData) {
            Variant v;
            memset(&v, 0, sizeof(v));
            HRESULT hr = immersive->GetImmersiveAttribute(attributes[i].attribute, &v);
            
            if (hr == S_OK && v.vt == blackmagicRawVariantTypeString && v.bstrVal) {
                const char* cStr = CFStringGetCStringPtr(v.bstrVal, kCFStringEncodingUTF8);
                if (cStr) {
                    ilpdContent = cStr;
                } else {
                    CFIndex length = CFStringGetLength(v.bstrVal);
                    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
                    char* buffer = new char[maxSize];
                    if (CFStringGetCString(v.bstrVal, buffer, maxSize, kCFStringEncodingUTF8)) {
                        ilpdContent = buffer;
                    }
                    delete[] buffer;
                }
                ilpdFound = true;
                std::cout << "Successfully retrieved OpticalProjectionData (type: " << v.vt << ")\n";
            } else {
                std::cout << "Failed to retrieve OpticalProjectionData\n";
            }
            VariantClear(&v);
            break;
        }
    }
    
    // Create the ILPD file with only the projection data
    if (ilpdFound && !ilpdContent.empty()) {
        std::ofstream ilpdOfs(outIlpd, std::ios::out);
        if (!ilpdOfs) {
            std::cerr << "Failed to create ILPD file: " << outIlpd << "\n";
            immersive->Release();
            clip->Release();
            codec->Release();
            factory->Release();
            return 7;
        }
        
        ilpdOfs << ilpdContent;
        ilpdOfs.close();
        std::cout << "ILPD projection data saved to: " << outIlpd << "\n";
    } else {
        std::cerr << "Warning: No OpticalProjectionData found, ILPD file not created\n";
    }

    // Only create detailed attributes file if -a option is used
    if (outputAllAttributes) {
        // Create detailed attributes file name
        std::string detailedFilename = outIlpd;
        size_t pos = detailedFilename.find_last_of('.');
        if (pos != std::string::npos) {
            detailedFilename = detailedFilename.substr(0, pos) + "_detailed_attributes.txt";
        } else {
            detailedFilename += "_detailed_attributes.txt";
        }
        
        // Create detailed attributes file
        std::ofstream detailedOfs(detailedFilename, std::ios::out);
        if (!detailedOfs) {
            std::cerr << "Failed to create detailed attributes file: " << detailedFilename << "\n";
            immersive->Release();
            clip->Release();
            codec->Release();
            factory->Release();
            return 7;
        }
        
        detailedOfs << "Complete Blackmagic RAW Immersive Video Attribute List (Detailed)\n";
        detailedOfs << "=" << std::string(60, '=') << "\n\n";
        detailedOfs << "Input file: " << inputBraw << "\n";
        detailedOfs << "ILPD file: " << outIlpd << "\n";
        detailedOfs << "Generated on: " << __DATE__ << " " << __TIME__ << "\n\n";
    
        // Iterate through all attributes for detailed output
        for (size_t i = 0; i < sizeof(attributes) / sizeof(attributes[0]); i++) {
            Variant v;
            memset(&v, 0, sizeof(v));  // Initialize to zero
            HRESULT hr = immersive->GetImmersiveAttribute(attributes[i].attribute, &v);
            
            // Write attribute header with type information
            if (hr != S_OK) {
                detailedOfs << "[" << (i + 1) << "] " << attributes[i].name << " (type: Failed to retrieve)\n";
            } else {
                detailedOfs << "[" << (i + 1) << "] " << attributes[i].name << " (type: " << v.vt << ")\n";
            }
            
            detailedOfs << "Description: " << attributes[i].description << "\n";
            
            if (hr != S_OK) {
                detailedOfs << "Failed to retrieve (HRESULT: 0x" << std::hex << hr << std::dec << ")\n";
                std::cout << "Failed to retrieve " << attributes[i].name << "\n";
            } else {
                std::cout << "Successfully retrieved " << attributes[i].name << " (type: " << v.vt << ")\n";
                
                // Handle data type
                if (v.vt == blackmagicRawVariantTypeString && v.bstrVal) {
                    // Handle string type
                    const char* cStr = CFStringGetCStringPtr(v.bstrVal, kCFStringEncodingUTF8);
                    std::string content;
                    if (cStr) {
                        content = cStr;
                    } else {
                        CFIndex length = CFStringGetLength(v.bstrVal);
                        CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
                        char* buffer = new char[maxSize];
                        if (CFStringGetCString(v.bstrVal, buffer, maxSize, kCFStringEncodingUTF8)) {
                            content = buffer;
                        } else {
                            content = "[String conversion failed]";
                        }
                        delete[] buffer;
                    }
                    
                    detailedOfs << "String value: " << content << "\n";
                } else if (v.vt == blackmagicRawVariantTypeSafeArray && v.parray) {
                    // Handle SafeArray type
                    detailedOfs << "SafeArray details:\n";
                    detailedOfs << "  Element count: " << v.parray->bounds.cElements << "\n";
                    detailedOfs << "  Variant type: " << v.parray->variantType << "\n";
                    
                    if (v.parray->data && v.parray->bounds.cElements > 0) {
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
                        
                        uint32_t totalSize = elementSize * v.parray->bounds.cElements;
                        detailedOfs << "  Total size: " << totalSize << " bytes\n";
                        detailedOfs << "  Hex data (first 512 bytes): ";
                        
                        uint32_t displaySize = std::min(totalSize, 512u);
                        for (uint32_t j = 0; j < displaySize; j++) {
                            detailedOfs << std::hex << std::setfill('0') << std::setw(2) 
                                       << static_cast<unsigned>(v.parray->data[j]);
                            if (j < displaySize - 1) detailedOfs << " ";
                        }
                        if (displaySize < totalSize) {
                            detailedOfs << " ... (truncated)";
                        }
                        detailedOfs << std::dec << "\n";
                    }
                } else {
                    // Handle other basic types
                    switch (v.vt) {
                        case blackmagicRawVariantTypeEmpty:
                            detailedOfs << "Empty value\n"; break;
                        case blackmagicRawVariantTypeU8:
                            detailedOfs << "U8 value: " << static_cast<unsigned>(v.uiVal) << "\n"; break;
                        case blackmagicRawVariantTypeS16:
                            detailedOfs << "S16 value: " << v.iVal << "\n"; break;
                        case blackmagicRawVariantTypeU16:
                            detailedOfs << "U16 value: " << v.uiVal << "\n"; break;
                        case blackmagicRawVariantTypeS32:
                            detailedOfs << "S32 value: " << v.intVal << "\n"; break;
                        case blackmagicRawVariantTypeU32:
                            detailedOfs << "U32 value: " << v.uintVal << "\n"; break;
                        case blackmagicRawVariantTypeFloat32:
                            detailedOfs << "Float32 value: " << v.fltVal << "\n"; break;
                        case blackmagicRawVariantTypeFloat64:
                            detailedOfs << "Float64 value: " << v.dblVal << "\n"; break;
                        default:
                            detailedOfs << "Unknown type: " << v.vt << "\n"; break;
                    }
                }
            }
            
            detailedOfs << "\n" << std::string(40, '-') << "\n\n";
            
            // Clean up variable
            VariantClear(&v);
        }
        
        detailedOfs.close();
        std::cout << "Detailed attributes saved to: " << detailedFilename << "\n";
    }

    // Release resources
    immersive->Release();
    clip->Release();
    codec->Release();
    factory->Release();

    std::cout << "Extraction completed successfully!\n";
    return 0;
}
