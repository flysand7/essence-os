struct SuperBlockCommon {
	uint8_t jmp[3];
	uint8_t oemName[8];
	uint16_t bytesPerSector;
	uint8_t sectorsPerCluster;
	uint16_t reservedSectors;
	uint8_t fatCount;
	uint16_t rootDirectoryEntries;
	uint16_t totalSectors;
	uint8_t mediaDescriptor;
	uint16_t sectorsPerFAT16;
	uint16_t sectorsPerTrack;
	uint16_t heads;
	uint32_t hiddenSectors;
	uint32_t largeSectorCount;
} ES_STRUCT_PACKED;

struct SuperBlock16 : SuperBlockCommon {
	uint8_t deviceID;
	uint8_t flags;
	uint8_t signature;
	uint32_t serial;
	uint8_t label[11];
	uint64_t systemIdentifier;
	uint8_t _unused1[450];
} ES_STRUCT_PACKED;

struct SuperBlock32 : SuperBlockCommon {
	uint32_t sectorsPerFAT32;
	uint16_t flags;
	uint16_t version;
	uint32_t rootDirectoryCluster;
	uint16_t fsInfoSector;
	uint16_t backupBootSector;
	uint8_t _unused0[12];
	uint8_t deviceID;
	uint8_t flags2;
	uint8_t signature;
	uint32_t serial;
	uint8_t label[11];
	uint64_t systemIdentifier;
	uint8_t _unused1[422];
} ES_STRUCT_PACKED;

struct FATDirectoryEntry {
	uint8_t name[11];
	uint8_t attributes;
	uint8_t _reserved0;
	uint8_t creationTimeSeconds;
	uint16_t creationTime;
	uint16_t creationDate;
	uint16_t accessedDate;
	uint16_t firstClusterHigh;
	uint16_t modificationTime;
	uint16_t modificationDate;
	uint16_t firstClusterLow;
	uint32_t fileSizeBytes;
} ES_STRUCT_PACKED;
