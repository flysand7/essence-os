#ifndef K_SIGNATURE_BLOCK_SIZE
#define K_SIGNATURE_BLOCK_SIZE (65536)
#endif

typedef struct MBRPartition {
	uint32_t offset, count;
	bool present;
} MBRPartition;

bool MBRGetPartitions(uint8_t *firstBlock, EsFileOffset sectorCount, MBRPartition *partitions /* 4 */) {
	if (firstBlock[510] != 0x55 || firstBlock[511] != 0xAA) {
		return false;
	}

#ifdef KERNEL
	KernelLog(LOG_INFO, "FS", "probing MBR", "First sector on device looks like an MBR...\n");
#endif

	for (uintptr_t i = 0; i < 4; i++) {
		if (!firstBlock[4 + 0x1BE + i * 0x10]) {
			partitions[i].present = false;
			continue;
		}

		partitions[i].offset =  
			  ((uint32_t) firstBlock[0x1BE + i * 0x10 + 8 ] <<  0)
			+ ((uint32_t) firstBlock[0x1BE + i * 0x10 + 9 ] <<  8)
			+ ((uint32_t) firstBlock[0x1BE + i * 0x10 + 10] << 16)
			+ ((uint32_t) firstBlock[0x1BE + i * 0x10 + 11] << 24);
		partitions[i].count =
			  ((uint32_t) firstBlock[0x1BE + i * 0x10 + 12] <<  0)
			+ ((uint32_t) firstBlock[0x1BE + i * 0x10 + 13] <<  8)
			+ ((uint32_t) firstBlock[0x1BE + i * 0x10 + 14] << 16)
			+ ((uint32_t) firstBlock[0x1BE + i * 0x10 + 15] << 24);
		partitions[i].present = true;

		if (partitions[i].offset > sectorCount || partitions[i].count > sectorCount - partitions[i].offset || partitions[i].count < 32) {
#ifdef KERNEL
			KernelLog(LOG_INFO, "FS", "invalid MBR", "Partition %d has offset %d and count %d, which is invalid. Ignoring the rest of the MBR...\n",
					i, partitions[i].offset, partitions[i].count);
#endif
			return false;
		}
	}

	return true;
}

void MBRFixPartition(uint32_t *partitions) {
	uint32_t headsPerCylinder = 256, sectorsPerTrack = 63;
	uint32_t partitionOffsetCylinder = (partitions[2] / sectorsPerTrack) / headsPerCylinder;
	uint32_t partitionOffsetHead = (partitions[2] / sectorsPerTrack) % headsPerCylinder;
	uint32_t partitionOffsetSector = (partitions[2] % sectorsPerTrack) + 1;
	uint32_t partitionSizeCylinder = (partitions[3] / sectorsPerTrack) / headsPerCylinder;
	uint32_t partitionSizeHead = (partitions[3] / sectorsPerTrack) % headsPerCylinder;
	uint32_t partitionSizeSector = (partitions[3] % sectorsPerTrack) + 1;
	partitions[0] |= (partitionOffsetHead << 8) | (partitionOffsetSector << 16) | (partitionOffsetCylinder << 24) | ((partitionOffsetCylinder >> 8) << 16);
	partitions[1] |= (partitionSizeHead << 8) | (partitionSizeSector << 16) | (partitionSizeCylinder << 24) | ((partitionSizeCylinder >> 8) << 16);
}

#define GPT_PARTITION_COUNT (0x80)

typedef struct GPTPartition {
	uint64_t offset, count;
	bool present, isESP;
} GPTPartition;

bool GPTGetPartitions(uint8_t *block /* K_SIGNATURE_BLOCK_SIZE */, EsFileOffset sectorCount, 
		EsFileOffset sectorBytes, GPTPartition *partitions /* GPT_PARTITION_COUNT */) {
	for (uintptr_t i = 0; i < GPT_PARTITION_COUNT; i++) {
		partitions[i].present = false;
	}

	if (sectorBytes * 2 >= K_SIGNATURE_BLOCK_SIZE || sectorCount <= 2 || sectorBytes < 0x200) {
		return false;
	}

	const char *signature = "EFI PART";

	for (uintptr_t i = 0; i < 8; i++) {
		if (block[sectorBytes + i] != signature[i]) {
			return false;
		}
	}

	uint32_t partitionEntryCount = *(uint32_t *) (block + sectorBytes + 80);
	uint32_t partitionEntryBytes = *(uint32_t *) (block + sectorBytes + 84);

	if (partitionEntryBytes < 0x80 || partitionEntryBytes > sectorBytes || sectorBytes % partitionEntryBytes 
			|| partitionEntryCount == 0 || partitionEntryBytes > 0x1000 || partitionEntryCount > 0x1000
			|| partitionEntryBytes * partitionEntryCount / sectorBytes + 2 >= sectorCount) {
		return false;
	}

	if (partitionEntryCount > GPT_PARTITION_COUNT) {
		partitionEntryCount = GPT_PARTITION_COUNT;
	}

	bool foundESP = false;

	for (uintptr_t i = 0; i < partitionEntryCount; i++) {
		uint8_t *entry = block + sectorBytes * 2 + i * partitionEntryBytes;

		uint64_t guidLow = *(uint64_t *) (entry + 0);
		uint64_t guidHigh = *(uint64_t *) (entry + 8);
		uint64_t firstLBA = *(uint64_t *) (entry + 32);
		uint64_t lastLBA = *(uint64_t *) (entry + 40);

		if (!guidLow && !guidHigh) {
			continue;
		}

		if ((!guidLow && !guidHigh) || firstLBA >= sectorCount || lastLBA >= sectorCount || firstLBA >= lastLBA) {
			return false;
		}

		partitions[i].present = true;
		partitions[i].offset = firstLBA;
		partitions[i].count = lastLBA - firstLBA;
		partitions[i].isESP = guidLow == 0x11D2F81FC12A7328 && guidHigh == 0x3BC93EC9A0004BBA;

		if (partitions[i].isESP) {
			if (foundESP) {
				return false;
			} else {
				foundESP = true;
			}
		}
	}

	return true;
}
