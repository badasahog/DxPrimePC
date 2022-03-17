#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <malloc.h>
#include <fstream>
#include <zlib.h>
#include <vector>
#include <type_traits>
#include <string>
#include <fcntl.h>
#include <io.h>
#include <bitset>
#include <concepts>
#include <DirectXMath.h>
struct fourCC
{
	char data[4];
};
struct unnamedAsset
{
	int32_t compressionFlag;
	fourCC type;
	int32_t ID;
	int32_t size;
	int32_t offset;
};

inline float asFloat(uint32_t i) noexcept{ return reinterpret_cast<float&>(i); }

struct nativeObject
{
	std::vector<uint16_t> vertex_indices;
	std::vector<uint16_t> normal_indices;
	std::vector<uint16_t> color0_indices;
	std::vector<uint16_t> color1_indices;
	std::vector<uint16_t> tex0_indices;
	std::vector<uint16_t> tex1_indices;
	std::vector<uint16_t> tex2_indices;
	std::vector<uint16_t> tex3_indices;
	std::vector<uint16_t> tex4_indices;
	std::vector<uint16_t> tex5_indices;
	std::vector<uint16_t> tex6_indices;
};

struct unnamedAssetTableEntry
{
	uint32_t id;
	void* nativeData = nullptr;
	unnamedAsset* foreignData;
};

unnamedAssetTableEntry* unnamedAssetTable;
uint32_t unnamedAssetTable_entries = 0;

int Unpack565(uint8_t _In_ const* const &packed, uint8_t* _Out_ color) noexcept
{
	// build the packed value - GCN: indices reversed
	int value = (int)packed[1] | ((int)packed[0] << 8);

	// get the components in the stored range
	uint8_t red = (uint8_t)((value >> 11) & 0x1f);
	uint8_t green = (uint8_t)((value >> 5) & 0x3f);
	uint8_t blue = (uint8_t)(value & 0x1f);

	// scale up to 8 bits
	color[0] = (red << 3) | (red >> 2);
	color[1] = (green << 2) | (green >> 4);
	color[2] = (blue << 3) | (blue >> 2);
	color[3] = 255;

	// return the value
	return value;
}

void DecompressColorGCN(const uint32_t _In_ &texWidth, uint8_t* _Out_writes_bytes_all_(texWidth * 32) rgba, const void *const _In_ block)
{
	// get the block bytes
	const uint8_t *const bytes = reinterpret_cast<uint8_t const*>(block);

	// unpack the endpoints
	uint8_t codes[16];
	const int a = Unpack565(bytes, codes);
	const int b = Unpack565(bytes + 2, codes + 4);

	// generate the midpoints
	for (int i = 0; i < 3; ++i)
	{
		const int c = codes[i];
		const int d = codes[4 + i];

		if (a <= b)
		{
			codes[8 + i] = (uint8_t)((c + d) / 2);
			// GCN: Use midpoint RGB rather than black
			codes[12 + i] = codes[8 + i];
		}
		else
		{
			// GCN: 3/8 blend rather than 1/3
			codes[8 + i] = (uint8_t)((c * 5 + d * 3) >> 3);
			codes[12 + i] = (uint8_t)((c * 3 + d * 5) >> 3);
		}
	}

	// fill in alpha for the intermediate values
	codes[8 + 3] = 255;
	codes[12 + 3] = (a <= b) ? 0 : 255;

	// unpack the indices
	uint8_t indices[16];
	for (int i = 0; i < 4; ++i)
	{
		uint8_t* ind = indices + 4 * i;
		uint8_t packed = bytes[4 + i];

		// GCN: indices reversed
		ind[3] = packed & 0x3;
		ind[2] = (packed >> 2) & 0x3;
		ind[1] = (packed >> 4) & 0x3;
		ind[0] = (packed >> 6) & 0x3;
	}

	// store out the colours
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
		{
			uint8_t offset = 4 * indices[y * 4 + x];
			for (int j = 0; j < 4; ++j)
			{
				rgba[4 * ((y * texWidth + x)) + (j)] = codes[offset + j];//+ (i - 8 < 0 ? 0 : 128)
			}// - i % 4
		}
}


struct TXTR_header
{
	uint32_t format;
	uint16_t width;
	uint16_t height;
	uint32_t mipCount;
};
inline void parseTXTR(const uint8_t *const &source, void** dest)
{
	__assume(*dest == nullptr);
	
	const TXTR_header* const header = reinterpret_cast<const TXTR_header *const>(source);
	std::cout << std::hex << "format: ";
	switch (_byteswap_ulong(header->format))
	{
	case 0x0:
		std::cout << "I4\n";
		break;
	case 0x1:
		std::cout << "I8\n";
		break;
	case 0x2:
		std::cout << "IA4\n";
		break;
	case 0x3:
		std::cout << "IA8\n";
		break;
	case 0x4:
		std::cout << "C4\n";
		break;
	case 0x5:
		std::cout << "C8\n";
		break;
	case 0x6:
		std::cout << "C14x2\n";
		break;
	case 0x7:
		std::cout << "RGB565\n";
		break;
	case 0x8:
		std::cout << "RGB5A3\n";
		break;
	case 0x9:
		std::cout << "RGBA8\n";
		break;
	case 0xA:
		std::cout << "CMPR\n";
		break;
	}

	const uint16_t imageWidth = _byteswap_ushort(header->width);
	const uint16_t imageHeight = _byteswap_ushort(header->height);

	std::cout << std::dec << "width: " << imageWidth << '\n';

	std::cout << std::dec << "height: " << imageHeight << '\n';

	std::cout << std::dec << "mips: " << _byteswap_ulong(header->mipCount) << '\n';

	uint32_t subGetLoc = sizeof(TXTR_header);

	//tbd: set up custom allocator for this
	*dest = malloc(imageWidth * imageHeight * 4 + sizeof(imageWidth) + sizeof(imageHeight));

	uint8_t* const pixels = ((uint8_t*)*dest) + sizeof(imageWidth) + sizeof(imageHeight);

	*(reinterpret_cast<uint16_t*>(pixels)-2) = imageWidth;
	*(reinterpret_cast<uint16_t*>(pixels)-1) = imageHeight;


	for (int y = 0; y < imageHeight; y += 8)
	{
		for (int x = 0; x < imageWidth; x += 8)
		{
			//decode full dxt1 block, (4 sub blocks)
			DecompressColorGCN(imageWidth, &pixels[4 * (y * imageWidth + x)], &source[subGetLoc]);
			subGetLoc += 8;
			DecompressColorGCN(imageWidth, &pixels[4 * ((y)*imageWidth + (x + 4))], &source[subGetLoc]);
			subGetLoc += 8;
			DecompressColorGCN(imageWidth, &pixels[4 * ((y + 4) * imageWidth + (x))], &source[subGetLoc]);
			subGetLoc += 8;
			DecompressColorGCN(imageWidth, &pixels[4 * ((y + 4) * imageWidth + (x + 4))], &source[subGetLoc]);
			subGetLoc += 8;
		}
	}
}

struct STRGHeader
{
	uint32_t magic;
	uint32_t version;
	uint32_t languageCount;
	uint32_t stringCount;
};
struct STRGLanguage
{
	fourCC langID;
	uint32_t langStringsOffset;
};
using MPString = typename std::vector<std::wstring>;
inline void parseSTRG(const const uint8_t* const _In_ &source)
{

#if _DEBUG
	std::cout << std::hex << "magic: " << _byteswap_ulong(reinterpret_cast<const STRGHeader* const>(source)->magic) << '\n';
	std::cout << std::hex << "version: " << _byteswap_ulong(reinterpret_cast<const STRGHeader* const>(source)->version) << '\n';
	std::cout << std::hex << "language count: " << _byteswap_ulong(reinterpret_cast<const STRGHeader* const>(source)->languageCount) << '\n';
	std::cout << std::hex << "string count: " << _byteswap_ulong(reinterpret_cast<const STRGHeader* const>(source)->stringCount) << '\n';
#endif
	const uint32_t stringCount = _byteswap_ulong(reinterpret_cast<const STRGHeader* const>(source)->stringCount);
	const uint8_t* const languageTableStart = source + sizeof(STRGHeader);

#if _DEBUG
	for (int i = 0; i < _byteswap_ulong(reinterpret_cast<const STRGHeader*>(source)->languageCount); i++)
	{
		std::cout << "language ID: "
			<< reinterpret_cast<const STRGLanguage* const>(languageTableStart + i * sizeof(STRGLanguage))->langID.data[0]
			<< reinterpret_cast<const STRGLanguage* const>(languageTableStart + i * sizeof(STRGLanguage))->langID.data[1]
			<< reinterpret_cast<const STRGLanguage* const>(languageTableStart + i * sizeof(STRGLanguage))->langID.data[2]
			<< reinterpret_cast<const STRGLanguage* const>(languageTableStart + i * sizeof(STRGLanguage))->langID.data[3] << '\n';
		std::cout << std::hex << "strings table offset: "
			<< _byteswap_ulong(reinterpret_cast<const STRGLanguage*>(languageTableStart + i * sizeof(STRGLanguage))->langStringsOffset) << '\n';
	}
#endif
	const uint8_t* stringTableStart = languageTableStart + _byteswap_ulong(reinterpret_cast<const STRGHeader*>(source)->languageCount) * sizeof(STRGLanguage);
	for (int i = 0; i < _byteswap_ulong(reinterpret_cast<const STRGHeader*>(source)->languageCount); i++)
	{
#if _DEBUG
		std::cout << std::dec << "string table size: " << _byteswap_ulong(*reinterpret_cast<const uint32_t*>(stringTableStart)) << '\n';
		for (int j = 0; j < stringCount; j++)
		{
			std::cout << std::dec << "string " << j << " offset: "
				<< _byteswap_ulong(*reinterpret_cast<const uint32_t* const>(stringTableStart + sizeof(uint32_t) + sizeof(uint32_t) * j)) << '\n';
		}
#endif
		//const uint8_t* stringArrayStart = stringTableStart + sizeof(uint32_t) + sizeof(uint32_t) * stringCount;
		_setmode(_fileno(stdout), _O_U16TEXT);
		for (int j = 0; j < stringCount; j++)
		{

			const ptrdiff_t stringStart = _byteswap_ulong(*reinterpret_cast<const uint32_t* const>(stringTableStart + sizeof(uint32_t) + sizeof(uint32_t) * j));

			ptrdiff_t k = stringStart;
			while (*reinterpret_cast<const wchar_t* const>(stringTableStart + sizeof(uint32_t) + k))
			{
				std::wcout << std::hex << (wchar_t)_byteswap_ushort(*reinterpret_cast<const wchar_t* const>(stringTableStart + sizeof(uint32_t) + k));
				k += 2;
			}

			std::wcout << std::endl;
		}
		_setmode(_fileno(stdout), _O_TEXT);
		return;
	}
}

struct CMDL_header
{
	uint32_t magic;
	uint32_t version;
	uint32_t flag;
	float MAABB[6];
	uint32_t dataSectionCount;
	uint32_t materialSetCount;
	uint32_t dataSectionSize;
};

struct CMDL_materialSet
{
	uint32_t textureIDs;
	uint32_t materialEndOffsets;
};

struct CMDL_material
{
	uint32_t flags;
	uint32_t textureIndices;
	uint32_t vertexAttributeFlags;
	uint32_t groupIndex;
	uint32_t konstColors;
	uint16_t blendDestinationFactor;
	uint16_t blendSourceFactor;
	uint32_t reflectionIndirectTextureSlotIndex;
	uint32_t colorChannelFlags;
	uint32_t TEVStageCount;
};

struct CMDL_TEVStage
{
	uint32_t colorInputFlags;
	uint32_t alphaInputFlags;
	uint32_t colorCombineFlags;
	uint32_t alphaCombineFlags;
	uint8_t padding;
	uint8_t konstAlphaInput;
	uint8_t konstColorInput;
	uint8_t rasterizedColorInput;
};

struct CMDL_TEVTextureInput
{
	uint16_t padding;
	uint8_t textureTEVInput;
	uint8_t texCoordTEVInput;
};

struct CMDL_SurfaceHeader
{
	float centerPoint[3];
	uint32_t materialIndex;
	uint16_t mantissa;
	uint16_t displayListSize;
	uint32_t parentModelPointerStorage;
	uint32_t nextSurfacePointerStorage;
	uint32_t extraDataSize;
	float surfaceNormal[3];
};

struct CMDL_SurfaceOffsetsHeader
{
	uint32_t surfaceCount;
	uint32_t surfaceEndOffset;
};

struct native_vertex
{
	uint32_t vertexAttributeFlags;
	float pos[3];
};

struct native_index_set
{

};

struct CMDL_native
{
	std::vector<DirectX::XMFLOAT3> native_positions;
	std::vector<DirectX::XMFLOAT3> native_normals;
	std::vector<DirectX::XMFLOAT2> native_tex0;
	std::vector<DirectX::XMFLOAT2> native_tex1;
	std::vector<uint16_t> native_indices;
};

[[nodiscard]]
inline constexpr uint32_t pad32(const uint32_t& _In_ input) noexcept
{
	return input - input % 32 + 32;
}

template<typename pointer>
inline void movePtr(pointer& ptr, const int32_t& amount) noexcept requires std::is_pointer_v<pointer>
{
	const uint8_t* v = reinterpret_cast<const uint8_t*>(ptr) + amount;
	ptr = reinterpret_cast<const pointer>(v);
}

template<typename pointer>
[[nodiscard]]
inline pointer offsetPointer(const pointer ptr, const uint32_t& amount) noexcept requires (std::is_pointer_v<pointer> && !std::is_const_v<pointer>)
{
	const uint8_t* v = reinterpret_cast<const uint8_t*>(ptr) + amount;
	return reinterpret_cast<const pointer>(v);
}

template<typename pointer>
[[nodiscard]]
inline pointer offsetPointer(const pointer const ptr, const uint32_t& amount) noexcept requires std::is_pointer_v<pointer> && std::is_const_v<pointer>
{
	const uint8_t* v = reinterpret_cast<const uint8_t*>(ptr) + amount;
	return reinterpret_cast<const pointer>(v);
}
	
std::vector<uint32_t> v_strings;
inline void parseCMDL(const uint8_t * const& _In_ source, void** dest)
{
	const CMDL_header* header = reinterpret_cast<const CMDL_header*>(source);
	std::cout << std::hex << "magic: " << _byteswap_ulong(header->magic) << '\n';
	std::cout << std::hex << "version: " << _byteswap_ulong(header->version) << '\n';
	std::cout << std::hex << "flags: " << _byteswap_ulong(header->flag) << '\n';
	std::cout << std::dec << "MAABB[0]: " << asFloat(_byteswap_ulong(header->MAABB[0])) << '\n';
	std::cout << std::dec << "MAABB[1]: " << asFloat(_byteswap_ulong(header->MAABB[1])) << '\n';
	std::cout << std::dec << "MAABB[2]: " << asFloat(_byteswap_ulong(header->MAABB[2])) << '\n';
	std::cout << std::dec << "MAABB[3]: " << asFloat(_byteswap_ulong(header->MAABB[3])) << '\n';
	std::cout << std::dec << "MAABB[4]: " << asFloat(_byteswap_ulong(header->MAABB[4])) << '\n';
	std::cout << std::dec << "MAABB[5]: " << asFloat(_byteswap_ulong(header->MAABB[5])) << '\n';
	std::cout << std::dec << "data section count: " << _byteswap_ulong(header->dataSectionCount) << '\n';
	std::cout << std::dec << "material set count: " << _byteswap_ulong(header->materialSetCount) << '\n';
	std::vector<uint32_t> dataSectionSizes;

	for (int i = 0; i < _byteswap_ulong(header->dataSectionCount); i++)
	{
		std::cout << std::dec << "data section " << i << ": " << _byteswap_ulong(offsetPointer(header, i * sizeof(header->dataSectionSize))->dataSectionSize) << '\n';
		dataSectionSizes.push_back(_byteswap_ulong(offsetPointer(header, i * sizeof(header->dataSectionSize))->dataSectionSize));
	}

	const uint8_t* currentDataSection = source +
		pad32(sizeof(CMDL_header) +
			sizeof(header->dataSectionSize) * (_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->dataSectionCount))
		);
	std::vector<uint32_t> vertexAttributeFlags;
	const CMDL_materialSet* materialSet = reinterpret_cast<const CMDL_materialSet*>(currentDataSection);
	for (int materialSetIndex = 0; materialSetIndex < _byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount); materialSetIndex++)
	{
		std::wcout << "reading material set " << materialSetIndex << '\n';
		std::cout << "texture count: " << _byteswap_ulong(materialSet->textureIDs) << '\n';

		for (int i = 0; i < _byteswap_ulong(materialSet->textureIDs); i++)
		{
			std::cout << std::hex << "texture: " << _byteswap_ulong(offsetPointer(materialSet, sizeof(materialSet->textureIDs) + i * sizeof(materialSet->textureIDs))->textureIDs) << '\n';
		}

		movePtr(materialSet, _byteswap_ulong(materialSet->textureIDs) * sizeof(materialSet->textureIDs));

		std::cout << "material count: " << _byteswap_ulong(materialSet->materialEndOffsets) << '\n';
		std::vector<uint32_t> materialEndOffsets;
		for (int i = 0; i < _byteswap_ulong(materialSet->materialEndOffsets); i++)
		{
			std::cout << std::hex << "material end offset: "
				<< _byteswap_ulong(offsetPointer(materialSet, sizeof(materialSet->materialEndOffsets) + i * sizeof(materialSet->materialEndOffsets))->materialEndOffsets)
				<< '\n';
			materialEndOffsets.push_back(_byteswap_ulong(offsetPointer(materialSet, sizeof(materialSet->materialEndOffsets) + i * sizeof(materialSet->materialEndOffsets))->materialEndOffsets));

		}

		movePtr(materialSet, sizeof(materialSet->materialEndOffsets) * _byteswap_ulong(materialSet->materialEndOffsets));

		const CMDL_material* const material1 = reinterpret_cast<const CMDL_material*>(materialSet + 1);//move to whatever is after the material set, the dynamic sizes have already been accounted for
		const CMDL_material* currentMaterial = material1;
		for (int materialIndex = 0; materialIndex < materialEndOffsets.size(); materialIndex++)
		{

			std::cout << "flags: " << std::hex << std::bitset<sizeof(currentMaterial->flags) * 8>(_byteswap_ulong(currentMaterial->flags)) << '\n';
			const decltype(currentMaterial->flags) materialFlags = _byteswap_ulong(currentMaterial->flags);

			std::cout << "texture count: " << _byteswap_ulong(currentMaterial->textureIndices) << '\n';

			for (int i = 0; i < _byteswap_ulong(currentMaterial->textureIndices); i++)
			{
				std::cout << std::hex << "texture index: " << _byteswap_ulong(offsetPointer(currentMaterial, sizeof(currentMaterial->textureIndices) + i * sizeof(currentMaterial->textureIndices))->textureIndices) << '\n';
			}

			movePtr(currentMaterial, sizeof(currentMaterial->textureIndices) * _byteswap_ulong(currentMaterial->textureIndices));

			std::cout << "vertex attribute flags: " << std::bitset<sizeof(currentMaterial->vertexAttributeFlags) * 8>(_byteswap_ulong(currentMaterial->vertexAttributeFlags)) << '\n';
			vertexAttributeFlags.push_back(_byteswap_ulong(currentMaterial->vertexAttributeFlags));
			std::cout << "group index: " << _byteswap_ulong(currentMaterial->groupIndex) << '\n';
			if (materialFlags & 0x8)//konst colors flag
			{
				std::cout << "has konst colors\n";
				std::cout << "konst count: " << _byteswap_ulong(currentMaterial->konstColors) << '\n';

				for (int i = 0; i < _byteswap_ulong(currentMaterial->konstColors); i++)
				{
					std::cout << std::hex << "konst color: " << _byteswap_ulong(offsetPointer(currentMaterial, sizeof(currentMaterial->konstColors) + i * sizeof(currentMaterial->konstColors))->konstColors) << '\n';
				}
				movePtr(currentMaterial, sizeof(currentMaterial->konstColors) * _byteswap_ulong(currentMaterial->konstColors));
			}
			else
			{
				movePtr(currentMaterial, 0i32 - sizeof(currentMaterial->konstColors));//if there are no konst colors move the struct back to align
			}
			std::cout << "blend destination factor: " << _byteswap_ushort(currentMaterial->blendDestinationFactor) << '\n';
			std::cout << "blend source factor: " << _byteswap_ushort(currentMaterial->blendSourceFactor) << '\n';

			if (materialFlags & 0x400)//reflection indirect slot index flag
			{
				std::cout << "reflection indirect slot index: " << _byteswap_ulong(currentMaterial->reflectionIndirectTextureSlotIndex) << '\n';
			}
			else
			{
				movePtr(currentMaterial, 0i32 - sizeof(currentMaterial->reflectionIndirectTextureSlotIndex));//if there is no reflection indirect texture slot index move the struct back to align
			}

			std::cout << "color channels: " << _byteswap_ulong(currentMaterial->colorChannelFlags) << '\n';

			for (int i = 0; i < _byteswap_ulong(currentMaterial->colorChannelFlags); i++)
			{
				decltype(currentMaterial->colorChannelFlags) colorChannelFlags = _byteswap_ulong(offsetPointer(currentMaterial, sizeof(currentMaterial->colorChannelFlags) + i * sizeof(currentMaterial->colorChannelFlags))->colorChannelFlags);
				std::cout << std::hex << "color channel flags: " << colorChannelFlags << '\n';
				std::cout << std::hex << "Enable lighting (enable): " << ((colorChannelFlags & 0x1) ? 1 : 0) << '\n';
				std::cout << std::hex << "Ambient color source (ambsrc): " << ((colorChannelFlags & 0x2) ? 1 : 0) << '\n';
				std::cout << std::hex << "Material color source (matsrc): " << ((colorChannelFlags & 0x4) ? 1 : 0) << '\n';
				std::cout << std::hex << "Light mask; always 0, filled in at runtime: " << ((colorChannelFlags & 0x7F8) ? 1 : 0) << '\n';
			}

			movePtr(currentMaterial, sizeof(currentMaterial->colorChannelFlags) * _byteswap_ulong(currentMaterial->colorChannelFlags));

			std::cout << "TEV stage count: " << _byteswap_ulong(currentMaterial->TEVStageCount) << '\n';

			const CMDL_TEVStage* const TEVStage = reinterpret_cast<const CMDL_TEVStage*>(currentMaterial + 1);

			for (int i = 0; i < _byteswap_ulong(currentMaterial->TEVStageCount); i++)
			{
				std::cout << "Color Input Flags: "
					<< ((_byteswap_ulong(TEVStage->colorInputFlags) & 0b00000'00000'11111) >> 0) << ", "
					<< ((_byteswap_ulong(TEVStage->colorInputFlags) & 0b00000'11111'00000) >> 5) << ", "
					<< ((_byteswap_ulong(TEVStage->colorInputFlags) & 0b11111'00000'00000) >> 10) << '\n';
				std::cout << "Alpha Input Flags: "
					<< ((_byteswap_ulong(TEVStage->alphaInputFlags) & 0b00000'00000'11111) >> 0) << ", "
					<< ((_byteswap_ulong(TEVStage->alphaInputFlags) & 0b00000'11111'00000) >> 5) << ", "
					<< ((_byteswap_ulong(TEVStage->alphaInputFlags) & 0b11111'00000'00000) >> 10) << '\n';
				std::cout << "Color Combine Flags: " << _byteswap_ulong(TEVStage->colorCombineFlags) << '\n';
				std::cout << "Alpha Combine Flags: " << _byteswap_ulong(TEVStage->alphaCombineFlags) << '\n';
				std::cout << "Padding: " << (uint16_t)TEVStage->padding << '\n';
				std::cout << "Konst Alpha Input: " << (uint16_t)TEVStage->konstAlphaInput << '\n';
				std::cout << "Konst Color Input: " << (uint16_t)TEVStage->konstColorInput << '\n';
				std::cout << "Rasterized Color Input: " << (uint16_t)TEVStage->rasterizedColorInput << '\n';
			}
			currentMaterial = material1;
			movePtr(currentMaterial, materialEndOffsets[materialIndex]);
		}
		movePtr(currentDataSection, dataSectionSizes[materialSetIndex]);
		materialSet = reinterpret_cast<const CMDL_materialSet*>(currentDataSection);
	}

	const uint8_t* vertexCoords = currentDataSection;
	movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 0]);
	const uint8_t* normals = currentDataSection;
	movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 1]);
	const uint8_t* vertexColors = currentDataSection;
	movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 2]);
	const uint8_t* floatUVCoords = currentDataSection;
	movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 3]);
	const uint8_t* shortUVCoords = currentDataSection;
	movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 4]);

	std::cout << "coordinate count: " << std::dec << (normals - vertexCoords) / (sizeof(float) * 3) << '\n';
	std::cout << "coordinates location: " << std::hex << (vertexCoords - source) << '\n';
	//std::cout << "normal count: " << std::dec << (normals - vertexCoords) / sizeof(float) / 3 << '\n';
	std::cout << "color count: " << std::dec << (floatUVCoords - vertexColors) / sizeof(uint32_t) << '\n';
	std::cout << "Float UV Coordinate count: " << std::dec << (shortUVCoords - floatUVCoords) / sizeof(float) / 2 << '\n';
	std::cout << "Short UV Coordinate count: " << std::dec << (currentDataSection - shortUVCoords) / sizeof(short) / 2 << '\n';
	std::ofstream MyFile("object.obj");
	//print vertex coords:
	for (uint32_t i = 0; i < (normals - vertexCoords) / (sizeof(float) * 3); i++)
	{
		MyFile << "v " <<
			asFloat(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(vertexCoords + (i * 3 + 0) * sizeof(float)))) << ' ' <<
			asFloat(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(vertexCoords + (i * 3 + 1) * sizeof(float)))) << ' ' <<
			asFloat(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(vertexCoords + (i * 3 + 2) * sizeof(float)))) << '\n';
	}

	for (uint32_t i = 0; i < (shortUVCoords - floatUVCoords) / (sizeof(float) * 2); i++)
	{
		MyFile << "vt " <<
			asFloat(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(floatUVCoords + (i * 2 + 0) * sizeof(float)))) << ' ' <<
			asFloat(_byteswap_ulong(*reinterpret_cast<const uint32_t*>(floatUVCoords + (i * 2 + 1) * sizeof(float)))) << '\n';
	}


	//print float UV coords
	/*
		for (int i = 0; i < (shortUVCoords - floatUVCoords) / sizeof(float) / 2; i++)
		{
			std::cout << "vt " << *reinterpret_cast<float*>(floatUVCoords + i * 2 + 0)
				<< " " << *reinterpret_cast<float*>(floatUVCoords + i * 2 + 1) << '\n';
		}
	*/


	const CMDL_SurfaceOffsetsHeader* const surfaceOffsets = reinterpret_cast<const CMDL_SurfaceOffsetsHeader*const>(currentDataSection);
	std::cout << "surface count: " << std::dec << _byteswap_ulong(surfaceOffsets->surfaceCount) << '\n';

	std::vector<uint32_t> surfaceEndOffsets;
	for (int i = 0; i < _byteswap_ulong(surfaceOffsets->surfaceCount); i++)
	{
		std::cout << std::hex << "surface end offset: " << std::dec << _byteswap_ulong(offsetPointer(surfaceOffsets, i * sizeof(surfaceOffsets->surfaceEndOffset))->surfaceEndOffset) << '\n';
		surfaceEndOffsets.push_back(_byteswap_ulong(offsetPointer(surfaceOffsets, i * sizeof(surfaceOffsets->surfaceEndOffset))->surfaceEndOffset));
	}

	movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 5]);

	struct vertex_tempC
	{
		//position, normal, tex0, text1
		uint16_t position;
		uint16_t normal;
		uint16_t tex0;
		uint16_t tex1;
	};

	std::vector<vertex_tempC> tempVB;
	for (int surfaceNum = 0; surfaceNum < _byteswap_ulong(surfaceOffsets->surfaceCount); surfaceNum++)
	{
		const CMDL_SurfaceHeader* firstSurface = reinterpret_cast<const CMDL_SurfaceHeader*>(currentDataSection);
		std::cout << std::dec << "center point: ("
			<< asFloat(_byteswap_ulong(firstSurface->centerPoint[0])) << ", "
			<< asFloat(_byteswap_ulong(firstSurface->centerPoint[1])) << ", "
			<< asFloat(_byteswap_ulong(firstSurface->centerPoint[2])) << ")\n";
		std::cout << std::dec << "material index: " << _byteswap_ulong(firstSurface->materialIndex) << '\n';
		std::cout << std::dec << "mantissa: " << std::hex << _byteswap_ushort(firstSurface->mantissa) << '\n';
		std::cout << std::dec << "display list size: " << std::dec << _byteswap_ushort(firstSurface->displayListSize) << '\n';
		std::cout << std::dec << "parent model storage: " << std::dec << _byteswap_ulong(firstSurface->parentModelPointerStorage) << '\n';
		std::cout << std::dec << "next surface pointer storage: " << std::dec << _byteswap_ulong(firstSurface->nextSurfacePointerStorage) << '\n';
		std::cout << std::dec << "extra data size: " << std::dec << _byteswap_ulong(firstSurface->extraDataSize) << '\n';
		std::cout << std::dec << "surface normal: ("
			<< asFloat(_byteswap_ulong(firstSurface->surfaceNormal[0])) << ", "
			<< asFloat(_byteswap_ulong(firstSurface->surfaceNormal[1])) << ", "
			<< asFloat(_byteswap_ulong(firstSurface->surfaceNormal[2])) << ")\n";

		const uint32_t primitiveMaterialIndex = _byteswap_ulong(firstSurface->materialIndex);
		const uint8_t* GXPrimitive = currentDataSection + pad32(sizeof(CMDL_SurfaceHeader) + _byteswap_ulong(firstSurface->extraDataSize));
		movePtr(currentDataSection, dataSectionSizes[_byteswap_ulong(reinterpret_cast<const CMDL_header*>(source)->materialSetCount) + 6 + surfaceNum]);


		for (int primitive = 0; true; primitive++)
		{
			const uint8_t primitiveType = *GXPrimitive & 0b11111000;
			std::cout << "primitive type: ";
			switch (*GXPrimitive & 0b11111000)
			{
			case 0x80:
				std::cout << "Quads\n";
				break;
			case 0x90:
				std::cout << "Triangles\n";
				break;
			case 0x98:
				std::cout << "Triangle Strip\n";
				break;
			case 0xA0:
				std::cout << "Triangle Fan\n";
				break;
			case 0xA8:
				std::cout << "Lines\n";
				break;
			case 0xB0:
				std::cout << "Line Strip\n";
				break;
			case 0xB8:
				std::cout << "Points\n";
				break;
			default:
				std::cout << "Type not found!\n";
			}
			std::cout << "format: ";
			switch (*GXPrimitive & 0b00000111)
			{
			case 0:
				std::cout << "GX_NRM_XYZ / GX_F32 | GX_TEX_ST / GX_F32\n";
				break;
			case 1:
				std::cout << "GX_NRM_XYZ / GX_S16 | GX_TEX_ST / GX_F32\n";
				break;
			case 2:
				std::cout << "GX_NRM_XYZ / GX_S16 | GX_TEX_ST / GX_S16\n";
				break;
			default:
				std::cout << "format not found\n";
			}
			GXPrimitive++;
			std::cout << "vertex count: " << std::dec << _byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)) << '\n';
			const uint32_t vertexCount = _byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive));
			GXPrimitive += 2;
			//todo: make dynamic
			//std::vector<uint16_t> vertex_indices;
			//std::vector<uint16_t> tex0_indices;
			nativeObject obj;
			for (int i = 0; i < vertexCount; i++)
			{
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0x3) // Position
				{
					obj.vertex_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0xC) // Normal
				{
					obj.normal_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0x30) // Color 0
				{
					obj.color0_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0xC0) // Color 1
				{
					obj.color1_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0x300) // Texture 0
				{
					obj.tex0_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0xC00) // Texture 1
				{
					obj.tex1_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0x3000) // Texture 2
				{
					obj.tex2_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0xC000) // Texture 3
				{
					obj.tex3_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0x30000) // Texture 4
				{
					obj.tex4_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0xC0000) // Texture 5
				{
					obj.tex5_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
				if (vertexAttributeFlags[primitiveMaterialIndex] & 0x300000) // Texture 6
				{
					obj.tex6_indices.push_back(_byteswap_ushort(*reinterpret_cast<const uint16_t*>(GXPrimitive)));
					GXPrimitive += 2;
				}
			}
			std::cout << __LINE__ << " " << vertexCount << '\n';
			for (int i = 0; i < vertexCount; i++)
			{
				if (primitiveType == 0x98)
				{
					if (i >= 2)
					{
						tempVB.push_back
						({
							obj.vertex_indices[i - 2], 
							obj.normal_indices[i - 2],
							obj.tex0_indices[i - 2],
							obj.tex1_indices[i - 2]
						});
						tempVB.push_back
						({
							obj.vertex_indices[i - 1],
							obj.normal_indices[i - 1],
							obj.tex0_indices[i - 1],
							obj.tex1_indices[i - 1]
						});
						tempVB.push_back
						({
							obj.vertex_indices[i - 0],
							obj.normal_indices[i - 0],
							obj.tex0_indices[i - 0],
							obj.tex1_indices[i - 0]
						});
						//MyFile << "f "
						//	<< (obj.vertex_indices[i - 2] + 1) << '/' << (obj.tex0_indices[i - 2] + 1) << ' '
						//	<< (obj.vertex_indices[i - 1] + 1) << '/' << (obj.tex0_indices[i - 1] + 1) << ' '
						//	<< (obj.vertex_indices[i - 0] + 1) << '/' << (obj.tex0_indices[i - 0] + 1) << '\n';
					}
				}
				else if (primitiveType == 0x90)
				{
					if (i % 3 == 2)
					{
						tempVB.push_back
						({
							obj.vertex_indices[i - 2],
							obj.normal_indices[i - 2],
							obj.tex0_indices[i - 2],
							obj.tex1_indices[i - 2]
						});
						tempVB.push_back
						({
							obj.vertex_indices[i - 1],
							obj.normal_indices[i - 1],
							obj.tex0_indices[i - 1],
							obj.tex1_indices[i - 1]
						});
						tempVB.push_back
						({
							obj.vertex_indices[i - 0],
							obj.normal_indices[i - 0],
							obj.tex0_indices[i - 0],
							obj.tex1_indices[i - 0]
						});
						//MyFile << "f "
						//	<< (obj.vertex_indices[i - 2] + 1) << '/' << (obj.tex0_indices[i - 2] + 1) << ' '
						//	<< (obj.vertex_indices[i - 1] + 1) << '/' << (obj.tex0_indices[i - 1] + 1) << ' '
						//	<< (obj.vertex_indices[i - 0] + 1) << '/' << (obj.tex0_indices[i - 0] + 1) << '\n';
					}
				}
				else if (primitiveType == 0xA0)
				{
					if (i >= 2)
					{
						tempVB.push_back
						({
							obj.vertex_indices[0],
							obj.normal_indices[0],
							obj.tex0_indices[0],
							obj.tex1_indices[0]
						});
						tempVB.push_back
						({
							obj.vertex_indices[i - 1],
							obj.normal_indices[i - 1],
							obj.tex0_indices[i - 1],
							obj.tex1_indices[i - 1]
						});
						tempVB.push_back
						({
							obj.vertex_indices[i - 0],
							obj.normal_indices[i - 0],
							obj.tex0_indices[i - 0],
							obj.tex1_indices[i - 0]
						});
						//MyFile << "f "
						//	<< (obj.vertex_indices[0] + 1) << '/' << (obj.tex0_indices[0] + 1) << ' '
						//	<< (obj.vertex_indices[i - 1] + 1) << '/' << (obj.tex0_indices[i - 1] + 1) << ' '
						//	<< (obj.vertex_indices[i - 0] + 1) << '/' << (obj.tex0_indices[i - 0] + 1) << '\n';
					}
				}
			}
			if (currentDataSection - GXPrimitive < 35)
				break;
			//else
			//{
				//std::cout << "NUM LEFT: " << (currentDataSection - GXPrimitive) << '\n';
			//}
		}
		
	}
	/*
		* tempVB is a buffer of 16 bit indices which needs to be converted to the PC format.
		* this is where the classic problem starts
		*
		* resource buffers:
		*
		* vertexCoords
		* normals
		* vertexColors
		* floatUVCoords
		* shortUVCoords
		*
		* unavoidably, the resource buffers need to be rebuilt
		*
		*/

		//at the end these structures will be done:

		//std::vector<DirectX::XMFLOAT3> native_positions;
		//std::vector<DirectX::XMFLOAT3> native_normals;
		//std::vector<DirectX::XMFLOAT2> native_tex0;
		//std::vector<DirectX::XMFLOAT2> native_tex1;
		//std::vector<uint16_t> native_indices;
	* dest = new CMDL_native();//malloc(sizeof(CMDL_native));

	CMDL_native* nativeObj = reinterpret_cast<CMDL_native*>(*dest);
	//right now I need to make this out of these:
	/*
	*
	* vertexCoords
	* normals
	* vertexColors
	* floatUVCoords
	* shortUVCoords
	* tempVB
	*
	*/
	nativeObj->native_indices.resize(tempVB.size());
	nativeObj->native_positions.resize(tempVB.size());
	nativeObj->native_normals.resize(tempVB.size());
	nativeObj->native_tex0.resize(tempVB.size());
	nativeObj->native_tex1.resize(tempVB.size());
	for (uint16_t i = 0; i < tempVB.size(); i++)
	{
		nativeObj->native_indices[i] = i;
		//std::cout << i << "<=>" << nativeObj->native_indices[i] << '\n';
		nativeObj->native_positions[i] = reinterpret_cast<const DirectX::XMFLOAT3*>(vertexCoords)[tempVB[i].position];
		nativeObj->native_normals[i] = reinterpret_cast<const DirectX::XMFLOAT3*>(normals)[tempVB[i].normal];
		nativeObj->native_tex0[i] = reinterpret_cast<const DirectX::XMFLOAT2*>(floatUVCoords)[tempVB[i].tex0];
		nativeObj->native_tex1[i] = reinterpret_cast<const DirectX::XMFLOAT2*>(floatUVCoords)[tempVB[i].tex1];
	}
	std::cout << "yni3: " << nativeObj->native_indices[25] << '\n';
	MyFile << std::flush;
	MyFile.close();
	return;

}
uint32_t temp_size = 0;
uint8_t* indexPak(const wchar_t* const& _In_ pakname)
{
	HANDLE file = CreateFileW(
		pakname,
		GENERIC_READ,
		0,
		nullptr,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		std::cout << "couldn't find file #@!@#" << '\n';
		return nullptr;
	}
	HANDLE hMapFile = CreateFileMappingW(
		file,          // current file handle
		nullptr,       // default security
		PAGE_READONLY, // read/write permission
		0,             // size of mapping object, high
		0,			   // size of mapping object, low
		nullptr);
	if (hMapFile == INVALID_HANDLE_VALUE)
	{
		std::cout << "couldn't map file" << '\n';
		return nullptr;
	}
	__assume(hMapFile != 0);
	uint8_t* address = (uint8_t*)(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0));
	__assume(address != nullptr);

	std::cout << std::hex << _byteswap_ushort(*((uint16_t*)(address + 0x0))) << '\n';
	std::cout << std::hex << _byteswap_ushort(*((uint16_t*)(address + 0x2))) << '\n';
	std::cout << std::hex << _byteswap_ulong(*((uint32_t*)(address + 0x4))) << '\n';
	std::cout << std::hex << _byteswap_ulong(*((uint32_t*)(address + 0x8))) << '\n';
	const uint32_t namedResources = _byteswap_ulong(*((uint32_t*)((uint8_t*)(address)+0x8)));

	ptrdiff_t offset = 0xC;

	for (int i = 0; i < namedResources; i++)
	{
		for (int x = 0; x < 4; x++)
		{
			std::cout << *((char*)address + offset + x);
		}
		std::cout << '\n';
		std::cout << std::hex << _byteswap_ulong(*((uint32_t*)(address + offset + 0x4))) << '\n';
		for (int x = 0; x < _byteswap_ulong(*((uint32_t*)(address + offset + 0x8))); x++)
		{
			std::cout << *((char*)address + offset + 0xC + x);
		}
		std::cout << '\n';
		offset += 0xCLL + _byteswap_ulong(*((uint32_t*)(address + offset + 0x8)));
	}
	const uint32_t unnamedResources = _byteswap_ulong(*((uint32_t*)(address + offset)));
	unnamedAssetTable_entries += unnamedResources;
	std::cout << std::dec << "unnamed resources: " << unnamedResources << '\n';
	offset += 4;

	unnamedAssetTable = reinterpret_cast<unnamedAssetTableEntry*>(VirtualAlloc(nullptr, unnamedResources * sizeof(unnamedAssetTableEntry), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	const unnamedAsset* unnamedResourceAnchor = reinterpret_cast<const unnamedAsset*>(address + offset);

	int count_CMDL = 0;
	int count_TXTR = 0;
	int count_MAPW = 0;
	int count_MAPA = 0;
	int count_ANIM = 0;
	int count_SCAN = 0;
	int count_MLVL = 0;
	int count_STRG = 0;
	int count_CSKR = 0;
	int count_CINF = 0;
	int count_PART = 0;
	int count_EVNT = 0;
	int count_ANCS = 0;
	int count_PATH = 0;
	int count_DCLN = 0;
	int count_MREA = 0;
	int count_FRME = 0;
	int count_FONT = 0;
	int count_SWHC = 0;
	int count_SAVW = 0;
	int count_AFSM = 0;
	int count_ELSC = 0;
	int count_CRSC = 0;
	int count_WPSC = 0;
	int count_DPSC = 0;
	int count_DGRP = 0;
	int count_OTHR = 0;


	std::vector<int32_t> hashTable;
	for (int i = 0; i < unnamedResources; i++)
	{
		//std::cout << "index: " << i << std::endl;
		//std::cout << "compression flag: " << std::hex << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->compressionFlag) << '\n';
		int uniqueAssetIndex = 0;//used for the indexing system
		fourCC temp = (reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->type);
		if (std::find(hashTable.begin(), hashTable.end(), _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID)) == hashTable.end())
		{
			hashTable.push_back(_byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID));
			unnamedAssetTable[hashTable.size()].id = _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID);
			unnamedAssetTable[hashTable.size()].foreignData = reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset));
			unnamedAssetTable[hashTable.size()].nativeData = nullptr;
		}
		else
		{
			continue;
		}

		if (temp.data[0] == 'C' && temp.data[1] == 'M' && temp.data[2] == 'D' && temp.data[3] == 'L')
		{
			//if (_byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->size) > temp_size)
			//{
				//std::cout << std::hex << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID) << ": " << std::dec << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->size) << '\n';
				//temp_size = _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->size);
			//}
			count_CMDL++;
		}
		else if (temp.data[0] == 'T' && temp.data[1] == 'X' && temp.data[2] == 'T' && temp.data[3] == 'R')count_TXTR++;
		else if (temp.data[0] == 'M' && temp.data[1] == 'A' && temp.data[2] == 'P' && temp.data[3] == 'W')count_MAPW++;
		else if (temp.data[0] == 'M' && temp.data[1] == 'A' && temp.data[2] == 'P' && temp.data[3] == 'A')count_MAPA++;
		else if (temp.data[0] == 'A' && temp.data[1] == 'N' && temp.data[2] == 'I' && temp.data[3] == 'M')count_ANIM++;
		else if (temp.data[0] == 'S' && temp.data[1] == 'C' && temp.data[2] == 'A' && temp.data[3] == 'N')
		{
			//std::cout << std::hex << "scan ID: " << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID) << '\n';
			count_SCAN++;
		}
		else if (temp.data[0] == 'M' && temp.data[1] == 'L' && temp.data[2] == 'V' && temp.data[3] == 'L')count_MLVL++;
		else if (temp.data[0] == 'S' && temp.data[1] == 'T' && temp.data[2] == 'R' && temp.data[3] == 'G')
		{
			v_strings.push_back(_byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID));
			count_STRG++;
		}
		else if (temp.data[0] == 'C' && temp.data[1] == 'S' && temp.data[2] == 'K' && temp.data[3] == 'R')count_CSKR++;
		else if (temp.data[0] == 'C' && temp.data[1] == 'I' && temp.data[2] == 'N' && temp.data[3] == 'F')count_CINF++;
		else if (temp.data[0] == 'P' && temp.data[1] == 'A' && temp.data[2] == 'R' && temp.data[3] == 'T')count_PART++;
		else if (temp.data[0] == 'E' && temp.data[1] == 'V' && temp.data[2] == 'N' && temp.data[3] == 'T')count_EVNT++;
		else if (temp.data[0] == 'A' && temp.data[1] == 'N' && temp.data[2] == 'C' && temp.data[3] == 'S')count_ANCS++;
		else if (temp.data[0] == 'P' && temp.data[1] == 'A' && temp.data[2] == 'T' && temp.data[3] == 'H')count_PATH++;
		else if (temp.data[0] == 'D' && temp.data[1] == 'C' && temp.data[2] == 'L' && temp.data[3] == 'N')count_DCLN++;
		else if (temp.data[0] == 'M' && temp.data[1] == 'R' && temp.data[2] == 'E' && temp.data[3] == 'A')count_MREA++;
		else if (temp.data[0] == 'F' && temp.data[1] == 'R' && temp.data[2] == 'M' && temp.data[3] == 'E')count_FRME++;
		else if (temp.data[0] == 'F' && temp.data[1] == 'O' && temp.data[2] == 'N' && temp.data[3] == 'T')
		{
			//std::cout << std::hex << (_byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID)) << '\n';
			count_FONT++;
		}
		else if (temp.data[0] == 'S' && temp.data[1] == 'W' && temp.data[2] == 'H' && temp.data[3] == 'C')count_SWHC++;
		else if (temp.data[0] == 'S' && temp.data[1] == 'A' && temp.data[2] == 'V' && temp.data[3] == 'W')count_SAVW++;
		else if (temp.data[0] == 'A' && temp.data[1] == 'F' && temp.data[2] == 'S' && temp.data[3] == 'M')count_AFSM++;
		else if (temp.data[0] == 'E' && temp.data[1] == 'L' && temp.data[2] == 'S' && temp.data[3] == 'C')count_ELSC++;
		else if (temp.data[0] == 'C' && temp.data[1] == 'R' && temp.data[2] == 'S' && temp.data[3] == 'C')count_CRSC++;
		else if (temp.data[0] == 'W' && temp.data[1] == 'P' && temp.data[2] == 'S' && temp.data[3] == 'C')count_WPSC++;
		else if (temp.data[0] == 'D' && temp.data[1] == 'P' && temp.data[2] == 'S' && temp.data[3] == 'C')count_DPSC++;
		else if (temp.data[0] == 'D' && temp.data[1] == 'G' && temp.data[2] == 'R' && temp.data[3] == 'P')count_DGRP++;
		else
		{
			std::cout << "type: "
				<< reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->type.data[0]
				<< reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->type.data[1]
				<< reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->type.data[2]
				<< reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->type.data[3] << '\n';
			count_OTHR++;
		}

		//std::cout << "asset ID: " << std::hex << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->ID) << '\n';
		//std::cout << "size: " << std::dec << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->size) << '\n';
		//std::cout << "offset: " << std::hex << _byteswap_ulong(reinterpret_cast<unnamedAsset*>(address + offset + i * sizeof(unnamedAsset))->offset) << '\n';//first model should have an offset of 0x43780
	}
	offset += sizeof(unnamedAsset) * unnamedResources;
	std::cout << std::dec << "CMDL count: " << count_CMDL << '\n';
	std::cout << std::dec << "TXTR count: " << count_TXTR << '\n';
	std::cout << std::dec << "MAPW count: " << count_MAPW << '\n';
	std::cout << std::dec << "MAPA count: " << count_MAPA << '\n';
	std::cout << std::dec << "ANIM count: " << count_ANIM << '\n';
	std::cout << std::dec << "SCAN count: " << count_SCAN << '\n';
	std::cout << std::dec << "MLVL count: " << count_MLVL << '\n';
	std::cout << std::dec << "STRG count: " << count_STRG << '\n';
	std::cout << std::dec << "CSKR count: " << count_CSKR << '\n';
	std::cout << std::dec << "CINF count: " << count_CINF << '\n';
	std::cout << std::dec << "PART count: " << count_PART << '\n';
	std::cout << std::dec << "EVNT count: " << count_EVNT << '\n';
	std::cout << std::dec << "ANCS count: " << count_ANCS << '\n';
	std::cout << std::dec << "PATH count: " << count_PATH << '\n';
	std::cout << std::dec << "DCLN count: " << count_DCLN << '\n';
	std::cout << std::dec << "MREA count: " << count_MREA << '\n';
	std::cout << std::dec << "FRME count: " << count_FRME << '\n';
	std::cout << std::dec << "FONT count: " << count_FONT << '\n';
	std::cout << std::dec << "SWHC count: " << count_SWHC << '\n';
	std::cout << std::dec << "SAVW count: " << count_SAVW << '\n';
	std::cout << std::dec << "AFSM count: " << count_AFSM << '\n';
	std::cout << std::dec << "ELSC count: " << count_ELSC << '\n';
	std::cout << std::dec << "CRSC count: " << count_CRSC << '\n';
	std::cout << std::dec << "WPSC count: " << count_WPSC << '\n';
	std::cout << std::dec << "DPSC count: " << count_DPSC << '\n';
	std::cout << std::dec << "DGRP count: " << count_DGRP << '\n';
	std::cout << std::dec << "OTHR count: " << count_OTHR << '\n';
	std::cout << std::dec << "duplicates detected: " << (unnamedResources - hashTable.size()) << '\n';
	return address;
}
void* loadAsset(const uint8_t* pakBegin, const uint32_t& id)
{
	int targetIndex = 0;
	for (; targetIndex < unnamedAssetTable_entries; targetIndex++)
	{
		if (unnamedAssetTable[targetIndex].id == id)
		{
			if (unnamedAssetTable[targetIndex].nativeData != nullptr)
			{
				std::cout << "asset loaded from cache index " << targetIndex << '\n';

				return unnamedAssetTable[targetIndex].nativeData;

			}
			else goto found;
		}
	}
	std::cout << "asset not indexed\n";
	return nullptr;
found:
	uint8_t* uncompressed_data;
	if (unnamedAssetTable[targetIndex].foreignData->compressionFlag)
	{

		uint32_t uncompressed_size = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(pakBegin + _byteswap_ulong(unnamedAssetTable[targetIndex].foreignData->offset)));


		uncompressed_data = reinterpret_cast<uint8_t*>(malloc(uncompressed_size));

		int result = uncompress(
			uncompressed_data,
			reinterpret_cast<uLongf*>(&uncompressed_size),
			pakBegin + _byteswap_ulong(unnamedAssetTable[targetIndex].foreignData->offset) + sizeof(uint32_t),
			_byteswap_ulong(unnamedAssetTable[targetIndex].foreignData->size)
		);

		switch (result)
		{
		case Z_OK:std::cout << "successfully extracted\n"; break;
		case Z_STREAM_END:std::cout << "FATAL ERROR Z_STREAM_END\n"; break;
		case Z_NEED_DICT:std::cout << "FATAL ERROR Z_NEED_DICT\n"; break;
		case Z_ERRNO:std::cout << "FATAL ERROR Z_ERRNO\n"; break;
		case Z_STREAM_ERROR:std::cout << "FATAL ERROR Z_STREAM_ERROR\n"; break;
		case Z_DATA_ERROR:std::cout << "FATAL ERROR Z_DATA_ERROR\n"; break;
		case Z_MEM_ERROR:std::cout << "FATAL ERROR Z_MEM_ERROR\n"; break;
		case Z_BUF_ERROR:std::cout << "FATAL ERROR Z_BUF_ERROR\n"; break;
		case Z_VERSION_ERROR:std::cout << "FATAL ERROR Z_VERSION_ERROR\n"; break;
		}
	}
	else
	{
		const uint32_t uncompressed_size = _byteswap_ulong(unnamedAssetTable[targetIndex].foreignData->size);
		uncompressed_data = reinterpret_cast<uint8_t*>(malloc(uncompressed_size));

		memcpy(uncompressed_data, pakBegin + _byteswap_ulong(unnamedAssetTable[targetIndex].foreignData->offset), uncompressed_size);
	}
	if (unnamedAssetTable[targetIndex].foreignData->type.data[0] == 'S'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[1] == 'T'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[2] == 'R'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[3] == 'G')
	{
		parseSTRG(uncompressed_data);
	}
	else if (unnamedAssetTable[targetIndex].foreignData->type.data[0] == 'C'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[1] == 'M'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[2] == 'D'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[3] == 'L')
	{
		parseCMDL(uncompressed_data, &unnamedAssetTable[targetIndex].nativeData);
		std::cout << "yk7: " << reinterpret_cast<CMDL_native*>(unnamedAssetTable[targetIndex].nativeData)->native_indices[53] << '\n';
	}
	else if (unnamedAssetTable[targetIndex].foreignData->type.data[0] == 'T'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[1] == 'X'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[2] == 'T'
		&& unnamedAssetTable[targetIndex].foreignData->type.data[3] == 'R')
	{
		parseTXTR(uncompressed_data, &(unnamedAssetTable[targetIndex].nativeData));
	}
	//unnamedAssetTable[targetIndex].nativeData = uncompressed_data;//TODO: placeholder, requires proper parsing
	return unnamedAssetTable[targetIndex].nativeData;
}
//int main()
//{
	/*
	* investigate cmdl: 0xEC81CD52 / working
	* wasp: 0x729EA8BA (working) / working
	* door: 0xD3D3AB81 (working) / working
	* thorns: 0x072DD016 (working) / working
	* artifact door: 0x2C53ADCC (working)
	* breakable wall: 0x2E941B92 (working) / working
	* monster hand: 0xD54C7055 (working) / working
	* eyeball: 0x88A3396A (working) / working
	* bea: 0x2770FEDC (working, broken texture) / working
	* wasp hive: 0x6022F0CB (not working) / working
	* mouth hatch: 0x6A8E7A04 (working) / working
	* rock helmet: 0x0EA00982 (working) / working
	* goat skull: 0xCA89960D (not working) / working
	* soar thumb: 0xA5E493F4 (not working) / tbd
	* morph ball hatch: 0x73F36E22 (not working) / working
	* seesaw: 0x3431D141 (not working) / working
	* wall: 0x5E0270F9 (not working)
	* slug: 0xFE6F5D43 (not working)
	* beetle: 0xB3574D33 (working)
	* wall eyeball: 0x9527B462 (working)
	* spore: 0x04A61D88 (working)
	* puffy hand: 0xEDC82CFD (working)
	* lump: 0x469B71A0 (working)
	* egg: 0xC6D70851 (working)
	* small door: 0xE6C4B13E (not working)
	* telletubby head: 0x42CDB236 (working)
	* slug: 0x19A32D30 (working)
	* horn: 0x55BAB970 (working)
	* bolt: 0xBFE4DAA0 (working)
	* giant boss: 0xEC81CD52 (working)
	* grump: 0x00576D37
	* gravity suit: 0x57F5C930 / working
	* mlvl: 0x83F6FF6F
	* mlvl skybox cmdl: 0x817968F9
	* strg: 0x1A626AAC
	* STRG strg = *loadSTRG(0x1A626AAC, "Metroid2.pak");
	*
	* textures:
	* big, neon: 0xD82A3380
	* neon trees: 0x94AD11C1
	* 
	* cinf: 0x81E65611
	* anim: 0x8B3569CE
	*
	* SCAN:
	* no texture: 0x6EC6DAE2
	* w/ texture: 0x1795014B
	*
	*
	* images for testing on:
	* 0xAD4ED949
	* 0xB22871B2
	* 0x955C61A9
	* 0x0CD5FA2F
	*
	* Metroid3:
	* giant monster: 0x07D51E01 (multiple material sets)
	* tower: 0xD1BA6B82
	* tiny piece of phazon: 0x5F8D540D
	* circle: 0x32103B70
	*
	* Metroid7:
	* crab (big file): 0xBF9ADF32
	*
	* Metroid8:
	* samus helmet off: 0x1AC15FA2
	*
	* shared:
	* crate: 0x2A1651CD
	* samus power suit: 0x57F5C930
	*/
	//const uint8_t* pak2Begin = indexPak(L"Metroid2.pak");

	//loadAsset(pak2Begin, 0x3431D141);


	//loadAsset(pak2Begin, 0x729EA8BA);
	//loadAsset(pak2Begin, 0x729EA8BA);

	//for (int i = 0; i < v_strings.size(); i++)
	//{
	//	loadAsset(pakBegin, v_strings[i]);
	//}


	/*
	for (int index = 0; index < 5; index++)
	{
		std::cout << offset << std::endl;
		std::cout << "first object offset: " << std::hex << _byteswap_ulong(unnamedResourceAnchor[index].offset) << std::endl;
		std::cout << "first object uncompressed size: " << std::dec << _byteswap_ulong(*reinterpret_cast<uint32_t*>(address + (_byteswap_ulong(unnamedResourceAnchor[index].offset)))) << std::endl;
		std::cout << "first object compressed size: " << std::dec << _byteswap_ulong(unnamedResourceAnchor[index].size) << std::endl;
		std::cout << "first object offset: " << std::hex << _byteswap_ushort(*reinterpret_cast<uint16_t*>(address + (_byteswap_ulong(unnamedResourceAnchor[index].offset) + 4))) << std::endl;

		uLongf uncompressedSize =
			*reinterpret_cast<uint32_t*>(
				address + (_byteswap_ulong(unnamedResourceAnchor[index].offset))
				);

		uint8_t* uncompressedData =
			(uint8_t*)malloc(
				*reinterpret_cast<uint32_t*>(
					address + (_byteswap_ulong(unnamedResourceAnchor[index].offset))
					)
			);

		auto result = uncompress(
			uncompressedData,
			&uncompressedSize,
			address + _byteswap_ulong(unnamedResourceAnchor[index].offset) + 4,
			_byteswap_ulong(unnamedResourceAnchor[index].size)
		);


		//std::ofstream wf("student.dat", std::ios::out | std::ios::binary);
		//
		//wf.write((char*)uncompressedData, uncompressedSize);


		switch (result)
		{
			case Z_OK:std::cout << "successfully extracted" << std::endl; break;
			case Z_STREAM_END:std::cout << "FATAL ERROR Z_STREAM_END" << std::endl; break;
			case Z_NEED_DICT:std::cout << "FATAL ERROR Z_NEED_DICT" << std::endl; break;
			case Z_ERRNO:std::cout << "FATAL ERROR Z_ERRNO" << std::endl; break;
			case Z_STREAM_ERROR:std::cout << "FATAL ERROR Z_STREAM_ERROR" << std::endl; break;
			case Z_DATA_ERROR:std::cout << "FATAL ERROR Z_DATA_ERROR" << std::endl; break;
			case Z_MEM_ERROR:std::cout << "FATAL ERROR Z_MEM_ERROR" << std::endl; break;
			case Z_BUF_ERROR:std::cout << "FATAL ERROR Z_BUF_ERROR" << std::endl; break;
			case Z_VERSION_ERROR:std::cout << "FATAL ERROR Z_VERSION_ERROR" << std::endl; break;
		}
		std::cout << "format: " << _byteswap_ulong(reinterpret_cast<TXTRHeader*>(uncompressedData)->format) << '\n';
		std::cout << "width: " << std::dec << _byteswap_ushort(reinterpret_cast<TXTRHeader*>(uncompressedData)->width) << '\n';
		std::cout << "height: " << std::dec << _byteswap_ushort(reinterpret_cast<TXTRHeader*>(uncompressedData)->height) << '\n';
		std::cout << "mip count: " << _byteswap_ulong(reinterpret_cast<TXTRHeader*>(uncompressedData)->mipCount) << '\n';
		int image_width = _byteswap_ushort(reinterpret_cast<TXTRHeader*>(uncompressedData)->width);
		int image_height = _byteswap_ushort(reinterpret_cast<TXTRHeader*>(uncompressedData)->height);
		uint8_t* pixels = (uint8_t*)malloc(32 * image_width * image_height);
		int subGetLoc = sizeof(TXTRHeader);
		for (int y = 0; y < image_height; y += 8)
		{
			for (int x = 0; x < image_width; x += 8)
			{
				//decode full dxt1 block, (4 sub blocks)
				DecompressColourGCN(image_width, &pixels[4 * ((y + 0) * image_width + (x + 0))], &uncompressedData[subGetLoc]);
				subGetLoc += 8;
				DecompressColourGCN(image_width, &pixels[4 * ((y + 0) * image_width + (x + 4))], &uncompressedData[subGetLoc]);
				subGetLoc += 8;
				DecompressColourGCN(image_width, &pixels[4 * ((y + 4) * image_width + (x + 0))], &uncompressedData[subGetLoc]);
				subGetLoc += 8;
				DecompressColourGCN(image_width, &pixels[4 * ((y + 4) * image_width + (x + 4))], &uncompressedData[subGetLoc]);
				subGetLoc += 8;
			}

		}

		std::string filename = "TXTR_"+ std::to_string(index)+".bmp";

		int width = image_width;
		int height = image_height;
		uint8_t* image = (uint8_t*)malloc(height * width * BYTES_PER_PIXEL);
		char* imageFileName = (char*)(filename.c_str());
		int i, j;
		for (i = 0; i < height; i++)
		{
			for (j = 0; j < width; j++)
			{
				image[i * width * 3 + j * 3 + 2] = pixels[i * width * 4 + j * 4 + 0]; ///red
				image[i * width * 3 + j * 3 + 1] = pixels[i * width * 4 + j * 4 + 1]; ///green
				image[i * width * 3 + j * 3 + 0] = pixels[i * width * 4 + j * 4 + 2]; ///blue
			}
		}

		generateBitmapImage((unsigned char*)image, height, width, imageFileName);
		free(image);
		free(pixels);
		printf("Image generated!!");
	}
	*/

	//}