// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

struct FileTypeApplicationEntry {
	int64_t application;
	bool open;
};

struct FileType {
	Array<FileTypeApplicationEntry> applicationEntries; // One per application that is involved with this type.
	EsUniqueIdentifier identifier;
	char *name;
	size_t nameBytes;
	uint32_t iconID;
	bool textual;
	bool hasThumbnailGenerator; // TODO Allow applications to register their own thumbnail generators.
};

Array<FileType> knownFileTypes; 
HashStore<char, EsUniqueIdentifier> knownFileTypesByExtension;
EsBuffer fileTypesBuffer;

void AddKnownFileTypes() {
#define ADD_FILE_TYPE(_name, _iconID) \
	{ \
		FileType type = {}; \
		type.name = (char *) _name; \
		type.nameBytes = EsCStringLength(_name); \
		type.iconID = _iconID; \
		knownFileTypes.Add(type); \
	}

#define KNOWN_FILE_TYPE_DIRECTORY (0)
	ADD_FILE_TYPE(interfaceString_CommonItemFolder, ES_ICON_FOLDER);
#define KNOWN_FILE_TYPE_UNKNOWN (1)
	ADD_FILE_TYPE(interfaceString_CommonItemFile, ES_ICON_UNKNOWN);
#define KNOWN_FILE_TYPE_DRIVE_HDD (2)
	ADD_FILE_TYPE(interfaceString_CommonDriveHDD, ES_ICON_DRIVE_HARDDISK);
#define KNOWN_FILE_TYPE_DRIVE_SSD (3)
	ADD_FILE_TYPE(interfaceString_CommonDriveSSD, ES_ICON_DRIVE_HARDDISK_SOLIDSTATE);
#define KNOWN_FILE_TYPE_DRIVE_CDROM (4)
	ADD_FILE_TYPE(interfaceString_CommonDriveCDROM, ES_ICON_MEDIA_OPTICAL);
#define KNOWN_FILE_TYPE_DRIVE_USB_MASS_STORAGE (5)
	ADD_FILE_TYPE(interfaceString_CommonDriveUSBMassStorage, ES_ICON_DRIVE_REMOVABLE_MEDIA_USB);
#define KNOWN_FILE_TYPE_DRIVES_PAGE (6)
	ADD_FILE_TYPE(interfaceString_FileManagerDrivesPage, ES_ICON_COMPUTER_LAPTOP);
#define KNOWN_FILE_TYPE_SPECIAL_COUNT (7)

	fileTypesBuffer = { .canGrow = true };
	EsSystemConfigurationReadFileTypes(&fileTypesBuffer);

	EsINIState s = { .buffer = (char *) fileTypesBuffer.out, .bytes = fileTypesBuffer.bytes };
	FileType type = {};
	FileTypeApplicationEntry applicationEntry = {};
	bool specifiedTextual = false, specifiedHasThumbnailGenerator = false, hasUUID = false;
	const char *matchValue;
	size_t matchValueBytes = 0;
	
	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("name"))) {
			type.name = s.value, type.nameBytes = s.valueBytes;
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("uuid"))) {
			type.identifier = EsUniqueIdentifierParse(s.value, s.valueBytes);
			hasUUID = true;
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("icon"))) {
			type.iconID = EsIconIDFromString(s.value, s.valueBytes);
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("has_thumbnail_generator"))) {
			// TODO Proper thumbnail generator registrations.
			type.hasThumbnailGenerator = EsIntegerParse(s.value, s.valueBytes) != 0;
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("textual"))) {
			type.textual = EsIntegerParse(s.value, s.valueBytes) != 0;
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("application"))) {
			applicationEntry.application = EsIntegerParse(s.value, s.valueBytes);
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("actions"))) {
			uintptr_t p = 0, q = 0;

			while (q <= s.valueBytes) {
				if (q == s.valueBytes || s.value[q] == ',') {
					String string = StringFromLiteralWithSize(&s.value[p], q - p);
					p = q + 1;

					if (StringEquals(string, StringFromLiteral("open"))) {
						applicationEntry.open = true;
					} else {
						EsPrint("File Manager: Unknown file type entry action '%s'.\n", STRFMT(string));
					}
				}

				q++;
			}
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("match"))) {
			matchValue = s.value;
			matchValueBytes = s.valueBytes;
		}

		if (!EsINIPeek(&s) || !s.keyBytes) {
			EsUniqueIdentifier zeroIdentifier = {};

			if (EsMemoryCompare(&type.identifier, &zeroIdentifier, sizeof(EsUniqueIdentifier))) {
				bool typeFound = false;
				uint32_t typeIndex = 0;

				ES_MACRO_SEARCH(knownFileTypes.Length(), { 
					result = EsMemoryCompare(&type.identifier, &knownFileTypes[index].identifier, sizeof(EsUniqueIdentifier));
				}, typeIndex, typeFound);
				
				EsAssert(typeIndex >= KNOWN_FILE_TYPE_SPECIAL_COUNT);
				FileType *existing;

				if (!typeFound) {
					existing = knownFileTypes.Insert(type, typeIndex);
					EsPrint("File Manager: Type %I registered by application %d with name '%s'.\n",
							type.identifier, applicationEntry.application, type.nameBytes, type.name);
				} else {
					// TODO Priority system.
					existing = &knownFileTypes[typeIndex];
					EsAssert(0 == EsMemoryCompare(&existing->identifier, &type.identifier, sizeof(EsUniqueIdentifier)));
					if (type.nameBytes) existing->name = type.name, existing->nameBytes = type.nameBytes;
					if (type.iconID) existing->iconID = type.iconID;
					if (specifiedTextual) existing->textual = type.textual;
					if (specifiedHasThumbnailGenerator) existing->hasThumbnailGenerator = type.hasThumbnailGenerator;
					EsPrint("File Manager: Type %I updated by application %d with name '%s'.\n",
							type.identifier, applicationEntry.application, type.nameBytes, type.name);
				}

				existing->applicationEntries.Add(applicationEntry);

				if (matchValueBytes) {
					uintptr_t p = 0, q = 0;

					while (q <= matchValueBytes) {
						if (q == matchValueBytes || matchValue[q] == ',') {
							String string = StringFromLiteralWithSize(&matchValue[p], q - p);
							p = q + 1;

							if (StringStartsWith(string, StringFromLiteral("ext:"))) {
								String extension = StringSlice(string, 4, -1);
								bool isLower = true;

								for (uintptr_t i = 0; i < extension.bytes; i++) {
									if (EsCRTisupper(extension.text[i])) {
										isLower = false;
									}
								}

								if (extension.bytes && isLower) {
									EsPrint("File Manager: Matching extension '%s' as %I.\n",
											STRFMT(string), type.identifier);
									*knownFileTypesByExtension.Put(STRING(extension)) = type.identifier;
								} else {
									EsPrint("File Manager: Invalid file type entry extension match '%s'.\n", 
											STRFMT(string));
								}
							} else {
								EsPrint("File Manager: Unknown file type entry match '%s'.\n", STRFMT(string));
							}
						}

						q++;
					}
				}
			} else {
				// The UUID is invalid, ignore the entry.
				if (hasUUID) EsPrint("File Manager: Discarding file type entry with invalid UUID.\n");
			}

			EsMemoryZero(&type, sizeof(type));
			EsMemoryZero(&applicationEntry, sizeof(applicationEntry));
			specifiedTextual = false, specifiedHasThumbnailGenerator = false, hasUUID = false;
			matchValueBytes = 0;
		}
	}

	s = { .buffer = (char *) fileTypesBuffer.out, .bytes = fileTypesBuffer.bytes };

	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("application"))) {
			applicationEntry.application = EsIntegerParse(s.value, s.valueBytes);
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("opens_any_textual_file"))) {
			applicationEntry.open = true;
		}

		if (!EsINIPeek(&s) || !s.keyBytes) {
			if (applicationEntry.open) {
				for (uintptr_t i = 0; i < knownFileTypes.Length(); i++) {
					if (knownFileTypes[i].textual) {
						knownFileTypes[i].applicationEntries.Insert(applicationEntry, 0);
						EsPrint("File Manager: Type %I is textual, so application %d can open it.\n", 
								knownFileTypes[i].identifier, applicationEntry.application);
					}
				}
			}

			EsMemoryZero(&applicationEntry, sizeof(applicationEntry));
		}
	}
}

EsUniqueIdentifier FileTypeMatchByExtension(String name) {
	String extension = PathGetExtension(name);
	char buffer[32];
	uintptr_t i = 0;

	for (; i < extension.bytes && i < 32; i++) {
		buffer[i] = EsCRTtolower(extension.text[i]);
	}

	return knownFileTypesByExtension.Get1(buffer, i);
}

FileType *FolderEntryGetType(Folder *folder, FolderEntry *entry) {
	EsUniqueIdentifier zeroIdentifier = {};

	if (entry->isFolder) {
		if (folder->itemHandler->getFileType != NamespaceDefaultGetFileType) {
			String path = StringAllocateAndFormat("%s%s", STRFMT(folder->path), STRFMT(entry->GetInternalName()));
			FileType *type = &knownFileTypes[folder->itemHandler->getFileType(path)];
			StringDestroy(&path);
			return type;
		}

		return &knownFileTypes[KNOWN_FILE_TYPE_DIRECTORY];
	} 
	
	if (0 == EsMemoryCompare(&entry->contentType, &zeroIdentifier, sizeof(EsUniqueIdentifier))) {
		entry->contentType = FileTypeMatchByExtension(entry->GetName());
		entry->guessedContentType = true;
	} 

	if (0 == EsMemoryCompare(&entry->contentType, &zeroIdentifier, sizeof(EsUniqueIdentifier))) {
		return &knownFileTypes[KNOWN_FILE_TYPE_UNKNOWN];
	}

	bool typeFound = false;
	uint32_t typeIndex = 0;

	ES_MACRO_SEARCH(knownFileTypes.Length(), { 
		result = EsMemoryCompare(&entry->contentType, &knownFileTypes[index].identifier, sizeof(EsUniqueIdentifier));
	}, typeIndex, typeFound);

	return &knownFileTypes[typeFound ? typeIndex : KNOWN_FILE_TYPE_UNKNOWN];
}
