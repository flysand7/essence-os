struct FileType {
	char *name;
	size_t nameBytes;
	char *extension;
	size_t extensionBytes;
	uint32_t iconID;
	int64_t openHandler;

	// TODO Allow applications to register their own thumbnail generators.
	bool hasThumbnailGenerator;
};

Array<FileType> knownFileTypes; 
HashStore<char, uintptr_t /* index into knownFileTypes */> knownFileTypesByExtension;

void AddKnownFileTypes() {
#define ADD_FILE_TYPE(_extension, _name, _iconID) \
	{ \
		FileType type = {}; \
		type.name = (char *) _name; \
		type.nameBytes = EsCStringLength(_name); \
		type.extension = (char *) _extension; \
		type.extensionBytes = EsCStringLength(_extension); \
		type.iconID = _iconID; \
		uintptr_t index = knownFileTypes.Length(); \
		knownFileTypes.Add(type); \
		*knownFileTypesByExtension.Put(_extension, EsCStringLength(_extension)) = index; \
	}

#define KNOWN_FILE_TYPE_DIRECTORY (0)
	ADD_FILE_TYPE("", interfaceString_CommonItemFolder, ES_ICON_FOLDER);
#define KNOWN_FILE_TYPE_UNKNOWN (1)
	ADD_FILE_TYPE("", interfaceString_CommonItemFile, ES_ICON_UNKNOWN);
#define KNOWN_FILE_TYPE_DRIVE_HDD (2)
	ADD_FILE_TYPE("", interfaceString_CommonDriveHDD, ES_ICON_DRIVE_HARDDISK);
#define KNOWN_FILE_TYPE_DRIVE_SSD (3)
	ADD_FILE_TYPE("", interfaceString_CommonDriveSSD, ES_ICON_DRIVE_HARDDISK_SOLIDSTATE);
#define KNOWN_FILE_TYPE_DRIVE_CDROM (4)
	ADD_FILE_TYPE("", interfaceString_CommonDriveCDROM, ES_ICON_MEDIA_OPTICAL);
#define KNOWN_FILE_TYPE_DRIVE_USB_MASS_STORAGE (5)
	ADD_FILE_TYPE("", interfaceString_CommonDriveUSBMassStorage, ES_ICON_DRIVE_REMOVABLE_MEDIA_USB);
#define KNOWN_FILE_TYPE_DRIVES_PAGE (6)
	ADD_FILE_TYPE("", interfaceString_FileManagerDrivesPage, ES_ICON_COMPUTER_LAPTOP);

	EsBuffer buffer = { .canGrow = true };
	EsSystemConfigurationReadFileTypes(&buffer);
	EsINIState s = { .buffer = (char *) buffer.out, .bytes = buffer.bytes };
	FileType type = {};
	
	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("name"))) {
			type.name = s.value, type.nameBytes = s.valueBytes;
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("extension"))) {
			type.extension = s.value, type.extensionBytes = s.valueBytes;
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("open"))) {
			type.openHandler = EsIntegerParse(s.value, s.valueBytes);
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("icon"))) {
			type.iconID = EsIconIDFromString(s.value, s.valueBytes);
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("has_thumbnail_generator")) && EsIntegerParse(s.value, s.valueBytes)) {
			// TODO Proper thumbnail generator registrations.
			type.hasThumbnailGenerator = true;
		}

		if (!EsINIPeek(&s) || !s.keyBytes) {
			uintptr_t index = knownFileTypes.Length();
			knownFileTypes.Add(type);
			*knownFileTypesByExtension.Put(type.extension, type.extensionBytes) = index;
			EsMemoryZero(&type, sizeof(type));
		}
	}
}

FileType *FolderEntryGetType(Folder *folder, FolderEntry *entry) {
	if (entry->isFolder) {
		if (folder->itemHandler->getFileType != NamespaceDefaultGetFileType) {
			String path = StringAllocateAndFormat("%s%s", STRFMT(folder->path), STRFMT(entry->GetInternalName()));
			FileType *type = &knownFileTypes[folder->itemHandler->getFileType(path)];
			StringDestroy(&path);
			return type;
		} else {
			return &knownFileTypes[KNOWN_FILE_TYPE_DIRECTORY];
		}
	} else {
		String extension = entry->GetExtension();
		char buffer[32];
		uintptr_t i = 0;

		for (; i < extension.bytes && i < 32; i++) {
			if (EsCRTisupper(extension.text[i])) {
				buffer[i] = EsCRTtolower(extension.text[i]);
			} else {
				buffer[i] = extension.text[i];
			}
		}

		uintptr_t index = knownFileTypesByExtension.Get1(buffer, i);
		return &knownFileTypes[index ? index : KNOWN_FILE_TYPE_UNKNOWN];
	}
}

uint32_t IconFromDriveType(uint8_t driveType) {
	if (driveType == ES_DRIVE_TYPE_HDD             ) return ES_ICON_DRIVE_HARDDISK;
	if (driveType == ES_DRIVE_TYPE_SSD             ) return ES_ICON_DRIVE_HARDDISK_SOLIDSTATE;
	if (driveType == ES_DRIVE_TYPE_CDROM           ) return ES_ICON_MEDIA_OPTICAL;
	if (driveType == ES_DRIVE_TYPE_USB_MASS_STORAGE) return ES_ICON_DRIVE_REMOVABLE_MEDIA_USB;
	return ES_ICON_DRIVE_HARDDISK;
}
