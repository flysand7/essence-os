void CommandRename(Instance *instance, EsElement *, EsCommand *) {
	intptr_t index = -1;

	for (uintptr_t i = 0; i < instance->listContents.Length(); i++) {
		ListEntry *entry = &instance->listContents[i];

		if (entry->selected) {
			index = i;
			break;
		}
	}

	EsAssert(index != -1);

	instance->rename.textbox = EsListViewCreateInlineTextbox(instance->list, 0, index, ES_LIST_VIEW_INLINE_TEXTBOX_COPY_EXISTING_TEXT);
	instance->rename.index = index;

	FolderEntry *entry = instance->listContents[index].entry;

	if (entry->extensionOffset != entry->nameBytes) {
		// Don't include the file extension in the initial selection.
		EsTextboxSetSelection(instance->rename.textbox, 0, 0, 0, entry->extensionOffset - 1);
	}

	instance->rename.textbox->messageUser = [] (EsElement *element, EsMessage *message) {
		if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
			Instance *instance = element->instance;

			String name = {};
			name.text = EsTextboxGetContents((EsTextbox *) element, &name.bytes);
			name.allocated = name.bytes;

			if (!name.bytes || message->endEdit.rejected) {
				StringDestroy(&name);
			} else {
				FolderEntry *entry = instance->listContents[instance->rename.index].entry;
				String oldName = entry->GetName();

				BlockingTaskQueue(instance, {
					.string = name,
					.string2 = StringDuplicate(oldName),
					.cDescription = interfaceString_FileManagerRenameTask,

					.callback = [] (Instance *instance, Task *task) {
						if (StringEquals(task->string, task->string2)) {
							task->result = ES_SUCCESS;
						} else {
							task->result = instance->folder->itemHandler->renameItem(instance->folder, task->string2, task->string);
						}
					},

					.then = [] (Instance *instance, Task *task) {
						if (task->result != ES_SUCCESS) {
							InstanceReportError(instance, ERROR_RENAME_ITEM, task->result);
						} else {
							Folder *folder = instance->folder;

							size_t newPathBytes;
							char *newPath = EsStringAllocateAndFormat(&newPathBytes, "%s%s", STRFMT(instance->folder->path), STRFMT(task->string));
							size_t oldPathBytes;
							char *oldPath = EsStringAllocateAndFormat(&oldPathBytes, "%s%s", STRFMT(instance->folder->path), STRFMT(task->string2));

							FolderPathMoved(instance, { .text = oldPath, .bytes = oldPathBytes }, { .text = newPath, .bytes = newPathBytes });

							EsDirectoryChild information = {};
							EsPathQueryInformation(newPath, newPathBytes, &information);
							uint64_t id = FolderRemoveEntryAndUpdateInstances(folder, STRING(task->string2));
							FolderAddEntryAndUpdateInstances(folder, STRING(task->string), &information, instance, false, id);

							EsHeapFree(oldPath);
							EsHeapFree(newPath);
						}

						StringDestroy(&task->string);
						StringDestroy(&task->string2);
					},
				});
			}

			EsElementDestroy(element);
		}

		return 0;
	};
}

void CommandNewFolder(Instance *instance, EsElement *, EsCommand *) {
	String name = StringAllocateAndFormat("%z", interfaceString_FileManagerNewFolderName);

	BlockingTaskQueue(instance, {
		.string = name,
		.cDescription = interfaceString_FileManagerNewFolderTask,

		.callback = [] (Instance *instance, Task *task) {
			task->result = instance->folder->itemHandler->createChildFolder(instance->folder, &task->string, true);
		},

		.then = [] (Instance *instance, Task *task) {
			if (task->result != ES_SUCCESS) {
				InstanceReportError(instance, ERROR_NEW_FOLDER, task->result);
			} else {
				Folder *folder = instance->folder;
				EsDirectoryChild information = {};
				information.type = ES_NODE_DIRECTORY;
				FolderAddEntryAndUpdateInstances(folder, STRING(task->string), &information, instance, false);
				CommandRename(instance, nullptr, nullptr);
			}

			StringDestroy(&task->string);
		},
	});
}

void InstanceRegisterCommands(Instance *instance) {
	uint32_t stableCommandID = 1;

	EsCommandRegister(&instance->commandGoBackwards, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		EsAssert(instance->pathBackwardHistory.Length()); 
		HistoryEntry entry = instance->pathBackwardHistory.Pop();
		StringDestroy(&instance->delayedFocusItem);
		instance->delayedFocusItem = entry.focusedItem;
		InstanceLoadFolder(instance, entry.path, LOAD_FOLDER_BACK);
	}, stableCommandID++, "Backspace|Alt+Left");

	EsCommandRegister(&instance->commandGoForwards, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		EsAssert(instance->pathForwardHistory.Length());
		HistoryEntry entry = instance->pathForwardHistory.Pop();
		StringDestroy(&instance->delayedFocusItem);
		instance->delayedFocusItem = entry.focusedItem;
		InstanceLoadFolder(instance, entry.path, LOAD_FOLDER_FORWARD);
	}, stableCommandID++, "Alt+Right");

	EsCommandRegister(&instance->commandGoParent, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		String parent = PathGetParent(instance->folder->path);
		InstanceLoadFolder(instance, StringDuplicate(parent));
	}, stableCommandID++, "Alt+Up");

	EsCommandRegister(&instance->commandRefresh, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		EsMutexAcquire(&loadedFoldersMutex);
		FolderRefresh(instance->folder);
		EsMutexRelease(&loadedFoldersMutex);
	}, stableCommandID++, "F5");

	EsCommandRegister(&instance->commandNewFolder, instance, CommandNewFolder, stableCommandID++, "Ctrl+Shift+N");
	EsCommandRegister(&instance->commandRename, instance, CommandRename, stableCommandID++, "F2");

	EsCommandRegister(&instance->commandViewDetails, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		instance->viewSettings.viewType = VIEW_DETAILS;
		InstanceRefreshViewType(instance);
		InstanceViewSettingsUpdated(instance);
	}, stableCommandID++);

	EsCommandRegister(&instance->commandViewTiles, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		instance->viewSettings.viewType = VIEW_TILES;
		InstanceRefreshViewType(instance);
		InstanceViewSettingsUpdated(instance);
	}, stableCommandID++);

	EsCommandRegister(&instance->commandViewThumbnails, instance, [] (Instance *instance, EsElement *, EsCommand *) {
		instance->viewSettings.viewType = VIEW_THUMBNAILS;
		InstanceRefreshViewType(instance);
		InstanceViewSettingsUpdated(instance);
	}, stableCommandID++);

	EsCommandSetDisabled(&instance->commandViewDetails, false);
	EsCommandSetDisabled(&instance->commandViewTiles, false);
	EsCommandSetDisabled(&instance->commandViewThumbnails, false);

	EsCommandSetCheck(&instance->commandViewDetails, ES_CHECK_CHECKED, false);
}
