typedef struct MBRPartition {
	uint32_t offset, count;
	bool present;
} MBRPartition;

bool MBRGetPartitions(uint8_t *firstBlock, EsFileOffset sectorCount, MBRPartition *partitions) {
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
