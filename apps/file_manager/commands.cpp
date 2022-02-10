// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

void CommandRename(Instance *instance, EsElement *, EsCommand *) {
	// TODO Undo.

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
						if (instance->closed) {
							// Ignore.
						} else if (task->result != ES_SUCCESS) {
							InstanceReportError(instance, ERROR_RENAME_ITEM, task->result);
						} else {
							Folder *folder = instance->folder;

							size_t newPathBytes;
							char *newPath = EsStringAllocateAndFormat(&newPathBytes, "%s%s", STRFMT(instance->folder->path), STRFMT(task->string));
							size_t oldPathBytes;
							char *oldPath = EsStringAllocateAndFormat(&oldPathBytes, "%s%s", STRFMT(instance->folder->path), STRFMT(task->string2));

							FolderPathMoved({ .text = oldPath, .bytes = oldPathBytes }, { .text = newPath, .bytes = newPathBytes }, true);

							EsDirectoryChild information = {};
							EsPathQueryInformation(newPath, newPathBytes, &information);
							EsMutexAcquire(&folder->modifyEntriesMutex);
							EsAssert(folder->doneInitialEnumeration);
							uint64_t id = FolderRemoveEntryAndUpdateInstances(folder, STRING(task->string2));
							FolderAddEntryAndUpdateInstances(folder, STRING(task->string), &information, instance, id);
							EsMutexRelease(&folder->modifyEntriesMutex);

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
			if (instance->closed) {
				// Ignore.
			} else if (task->result != ES_SUCCESS) {
				InstanceReportError(instance, ERROR_NEW_FOLDER, task->result);
			} else {
				Folder *folder = instance->folder;
				EsDirectoryChild information = {};
				information.type = ES_NODE_DIRECTORY;
				EsMutexAcquire(&folder->modifyEntriesMutex);
				EsAssert(folder->doneInitialEnumeration);
				FolderAddEntryAndUpdateInstances(folder, STRING(task->string), &information, instance);
				EsMutexRelease(&folder->modifyEntriesMutex);
				CommandRename(instance, nullptr, nullptr);
			}

			StringDestroy(&task->string);
		},
	});
}

void CommandCopyOrCut(Instance *instance, uint32_t flags) {
	// TODO If copying a single file, copy the data of the file (as well as its path),
	// 	so that document can be pasted into other applications.
	
	uint8_t _buffer[4096];
	EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
	buffer.fileStore = EsClipboardOpen(ES_CLIPBOARD_PRIMARY);

	for (uintptr_t i = 0; i < instance->listContents.Length() && !buffer.error; i++) {
		if (instance->listContents[i].selected) {
			FolderEntry *entry = instance->listContents[i].entry;
			String path = instance->folder->itemHandler->getPathForChild(instance->folder, entry);
			EsBufferWrite(&buffer, STRING(path));
			StringDestroy(&path);
			uint8_t separator = '\n';
			EsBufferWrite(&buffer, &separator, 1);
		}
	}

	EsBufferFlushToFileStore(&buffer);

	EsPoint point = EsListViewGetAnnouncementPointForSelection(instance->list);
	EsError error = EsClipboardCloseAndAdd(ES_CLIPBOARD_PRIMARY, ES_CLIPBOARD_FORMAT_PATH_LIST, buffer.fileStore, flags);

	if (error == ES_SUCCESS) {
		if (flags & ES_CLIPBOARD_ADD_LAZY_CUT) {
			EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, point.x, point.y, INTERFACE_STRING(CommonAnnouncementCut));
		} else {
			EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, point.x, point.y, INTERFACE_STRING(CommonAnnouncementCopied));
		}
	} else if (error == ES_ERROR_INSUFFICIENT_RESOURCES || error == ES_ERROR_DRIVE_FULL) {
		EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, point.x, point.y, INTERFACE_STRING(CommonAnnouncementCopyErrorResources));
	} else {
		EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, point.x, point.y, INTERFACE_STRING(CommonAnnouncementCopyErrorOther));
	}
}

void CommandCut(Instance *instance, EsElement *, EsCommand *) {
	CommandCopyOrCut(instance, ES_CLIPBOARD_ADD_LAZY_CUT);
}

void CommandCopy(Instance *instance, EsElement *, EsCommand *) {
	CommandCopyOrCut(instance, ES_FLAGS_DEFAULT);
}

struct PasteOperation {
	String source, destination;
};

struct PasteTask {
	// Input:
	String destinationBase;
	bool move;
	char *pathList;
	size_t pathListBytes;

	// State:
	bool progressByData;
	EsFileOffset totalDataToProcess, totalDataProcessed;
	size_t sourceItemCount, sourceItemsProcessed;
	EsUserTask *userTask;
	EsFileOffset lastBytesCopied, cumulativeSecondBytesCopied;
	EsFileOffsetDifference bytesPerSecond;
	double cumulativeSecondTimeStampMs;
};

bool CommandPasteCopyCallback(EsFileOffset bytesCopied, EsFileOffset totalBytes, EsGeneric data) {
	(void) totalBytes;

	PasteTask *task = (PasteTask *) data.p;
	EsFileOffset delta = bytesCopied - task->lastBytesCopied;
	task->totalDataProcessed += delta;
	task->lastBytesCopied = bytesCopied;

	if (task->progressByData) {
		double timeStampMs = EsTimeStampMs();

		if (!task->cumulativeSecondTimeStampMs) {
			task->cumulativeSecondTimeStampMs = timeStampMs;
			task->bytesPerSecond = -1;
		} else if (timeStampMs - task->cumulativeSecondTimeStampMs > 1000.0) {
			// TODO Test that this calculation is correct.
			task->bytesPerSecond = task->cumulativeSecondBytesCopied / (timeStampMs - task->cumulativeSecondTimeStampMs);
			task->cumulativeSecondTimeStampMs = timeStampMs;
			task->cumulativeSecondBytesCopied = 0;
		}

		task->cumulativeSecondBytesCopied += delta;
		EsUserTaskSetProgress(task->userTask, (double) task->totalDataProcessed / task->totalDataToProcess, task->bytesPerSecond);
	}

	return EsUserTaskIsRunning(task->userTask);
}

EsError CommandPasteFile(String source, String destinationBase, void **copyBuffer, PasteTask *task, String *_destination) {
	if (!EsUserTaskIsRunning(task->userTask)) {
		return ES_ERROR_CANCELLED;
	}

	if (PathHasPrefix(destinationBase, source)) {
		return ES_ERROR_TARGET_WITHIN_SOURCE;
	}

	String name = PathGetName(source);
	String destination = StringAllocateAndFormat("%s%z%s", STRFMT(destinationBase), PathHasTrailingSlash(destinationBase) ? "" : "/", STRFMT(name));
	EsError error;

	if (StringEquals(PathGetParent(source), destinationBase)) {
		if (task->move) {
			// Move with the source and destination folders identical; meaningless.
			error = ES_SUCCESS;
			goto done;
		}

		destination.allocated += 32;
		destination.text = (char *) EsHeapReallocate(destination.text, destination.allocated, false);
		size_t bytes = EsPathFindUniqueName(destination.text, destination.bytes, destination.allocated);

		if (bytes) {
			destination.bytes = destination.allocated = bytes;
		} else {
			destination.allocated = destination.bytes;
			StringDestroy(&destination);
			return ES_ERROR_ALREADY_EXISTS;
		}
	}

	EsPrint("%z %s -> %s...\n", task->move ? "Moving" : "Copying", STRFMT(source), STRFMT(destination));

	if (task->move) {
		error = EsPathMove(STRING(source), STRING(destination), ES_FLAGS_DEFAULT);

		if (error == ES_ERROR_VOLUME_MISMATCH) {
			// TODO Delete the files after all copies complete successfully.
			goto copy;
		}
	} else {
		copy:;
		task->lastBytesCopied = 0;
		error = EsFileCopy(STRING(source), STRING(destination), copyBuffer, CommandPasteCopyCallback, task);
	}

	if (error == ES_ERROR_INCORRECT_NODE_TYPE) {
		uintptr_t childCount;
		EsDirectoryChild *buffer = EsDirectoryEnumerateChildren(STRING(source), &childCount, &error);

		if (error == ES_SUCCESS) {
			error = EsPathCreate(STRING(destination), ES_NODE_DIRECTORY, false);

			if (error == ES_SUCCESS) {
				for (uintptr_t i = 0; i < childCount && error == ES_SUCCESS; i++) {
					String childSourcePath = StringAllocateAndFormat("%s%z%s", STRFMT(source), 
							PathHasTrailingSlash(source) ? "" : "/", buffer[i].nameBytes, buffer[i].name);
					error = CommandPasteFile(childSourcePath, destination, copyBuffer, task, nullptr);
					StringDestroy(&childSourcePath);
				}
			}

			EsHeapFree(buffer);
		}
	}

	if (error == ES_SUCCESS) {
		EsMessageMutexAcquire();
		if (task->move) FolderFileUpdatedAtPath(source, nullptr);
		FolderFileUpdatedAtPath(destination, nullptr);
		EsMessageMutexRelease();
	}

	done:;

	if (_destination && error == ES_SUCCESS) {
		*_destination = destination;
	} else {
		StringDestroy(&destination);
	}

	return error;
}

void CommandPasteTask(EsUserTask *userTask, EsGeneric _task) {
	// TODO Reporting errors properly. Ask to retry or skip.
	// TODO If the destination file already exists, ask to rename or skip (as replace is destructive, it should be an advanced option).
	// TODO Other namespace handlers.
	// TODO Undo.

	PasteTask *task = (PasteTask *) _task.p;
	Array<PasteOperation> pasteOperations = {};
	EsError error = ES_SUCCESS;
	task->userTask = userTask;

	void *copyBuffer = nullptr;

	const char *position = task->pathList;
	size_t remainingBytes = task->pathListBytes;

	while (remainingBytes && EsUserTaskIsRunning(task->userTask)) {
		const char *newline = (const char *) EsCRTmemchr(position, '\n', remainingBytes); 
		if (!newline) break;

		String source = StringFromLiteralWithSize(position, newline - position);

		if (!task->move || !StringEquals(PathGetDrive(source), PathGetDrive(task->destinationBase))) {
			// Files are actually being copied, so report progress by the amount of data copied,
			// rather than the amount of files processed.
			task->progressByData = true;
		}

		EsDirectoryChild information;

		if (EsPathQueryInformation(STRING(source), &information)) {
			if (information.fileSize == -1) {
				// TODO Support progress on volumes that don't report total directory sizes.
			} else {
				task->totalDataToProcess += information.fileSize;
			}

			task->sourceItemCount++;
		} else {
			// We will probably error on this file, so ignore it.
		}

		position += source.bytes + 1;
		remainingBytes -= source.bytes + 1;
	}

	position = task->pathList;
	remainingBytes = task->pathListBytes;

	while (remainingBytes && EsUserTaskIsRunning(task->userTask)) {
		const char *newline = (const char *) EsCRTmemchr(position, '\n', remainingBytes); 
		if (!newline) break;

		String source = StringFromLiteralWithSize(position, newline - position);
		String destination;
		error = CommandPasteFile(source, task->destinationBase, &copyBuffer, task, &destination);
		if (error != ES_SUCCESS) break;

		PasteOperation operation = { .source = StringDuplicate(source), .destination = destination };
		pasteOperations.Add(operation);

		position += source.bytes + 1;
		remainingBytes -= source.bytes + 1;

		task->sourceItemsProcessed++;

		if (!task->progressByData) {
			EsUserTaskSetProgress(userTask, (double) task->sourceItemsProcessed / task->sourceItemCount, -1);
		}
	}

	EsMessageMutexAcquire();

	size_t pathSectionCount = PathCountSections(task->destinationBase);
	FolderFileUpdatedAtPath(PathGetDrive(task->destinationBase), nullptr);

	for (uintptr_t i = 0; i < pathSectionCount; i++) {
		String parent = PathGetParent(task->destinationBase, i + 1);
		FolderFileUpdatedAtPath(parent, nullptr);
	}

	if (task->move) {
		if (pasteOperations.Length()) {
			size_t pathSectionCount = PathCountSections(pasteOperations[0].source);
			FolderFileUpdatedAtPath(PathGetDrive(pasteOperations[0].source), nullptr);

			for (uintptr_t i = 0; i < pathSectionCount; i++) {
				String parent = PathGetParent(pasteOperations[0].source, i + 1);
				FolderFileUpdatedAtPath(parent, nullptr);
			}
		}

		for (uintptr_t i = 0; i < pasteOperations.Length(); i++) {
			FolderPathMoved(pasteOperations[i].source, pasteOperations[i].destination, i == pasteOperations.Length() - 1);
		}
	}

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		Instance *instance = instances[i];

		if (!instance->closed && instance->issuedPasteTask == task) {
			instance->issuedPasteTask = nullptr;

			if (error != ES_SUCCESS) {
				EsPoint point = EsListViewGetAnnouncementPointForSelection(instance->list);
				EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, point.x, point.y, INTERFACE_STRING(CommonAnnouncementPasteErrorOther));
			} else {
				EsListViewSelectNone(instance->list);

				for (uintptr_t i = 0; i < pasteOperations.Length(); i++) {
					String name = PathRemoveTrailingSlash(PathGetName(pasteOperations[i].destination));
					InstanceSelectByName(instance, name, true, i == pasteOperations.Length() - 1);
				}
			}
		}
	}

	EsMessageMutexRelease();

	for (uintptr_t i = 0; i < pasteOperations.Length(); i++) {
		StringDestroy(&pasteOperations[i].source);
		StringDestroy(&pasteOperations[i].destination);
	}

	pasteOperations.Free();
	EsHeapFree(copyBuffer);
	EsHeapFree(task->pathList);
	StringDestroy(&task->destinationBase);
	EsHeapFree(task);
}

void CommandPaste(Instance *instance, EsElement *, EsCommand *) {
	if (EsClipboardHasFormat(ES_CLIPBOARD_PRIMARY, ES_CLIPBOARD_FORMAT_PATH_LIST)) {
		PasteTask *task = (PasteTask *) EsHeapAllocate(sizeof(PasteTask), true);
		uint32_t flags;
		task->pathList = EsClipboardReadText(ES_CLIPBOARD_PRIMARY, &task->pathListBytes, &flags);
		task->move = flags & ES_CLIPBOARD_ADD_LAZY_CUT;
		task->destinationBase = StringDuplicate(instance->folder->path);
		instance->issuedPasteTask = task;

		EsError error;

		if (task->move) {
			error = EsUserTaskStart(CommandPasteTask, task, INTERFACE_STRING(FileManagerMoveTask), ES_ICON_FOLDER_MOVE);
		} else {
			error = EsUserTaskStart(CommandPasteTask, task, INTERFACE_STRING(FileManagerCopyTask), ES_ICON_FOLDER_COPY);
		}

		if (error != ES_SUCCESS) {
			EsPoint point = EsListViewGetAnnouncementPointForSelection(instance->list);
			EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, point.x, point.y, INTERFACE_STRING(CommonAnnouncementPasteErrorOther));
			EsHeapFree(task->pathList);
			StringDestroy(&task->destinationBase);
			EsHeapFree(task);
		}
	} else {
		// TODO Paste the data into a new file.
	}
}

void InstanceRegisterCommands(Instance *instance) {
	uint32_t stableCommandID = 1;

	EsCommandRegister(&instance->commandGoBackwards, instance, INTERFACE_STRING(FileManagerGoBack), [] (Instance *instance, EsElement *, EsCommand *) {
		EsAssert(instance->pathBackwardHistory.Length()); 
		HistoryEntry entry = instance->pathBackwardHistory.Pop();
		StringDestroy(&instance->delayedFocusItem);
		instance->delayedFocusItem = entry.focusedItem;
		InstanceLoadFolder(instance, entry.path, LOAD_FOLDER_BACK);
	}, stableCommandID++, "Backspace|Alt+Left");

	EsCommandRegister(&instance->commandGoForwards, instance, INTERFACE_STRING(FileManagerGoForwards), [] (Instance *instance, EsElement *, EsCommand *) {
		EsAssert(instance->pathForwardHistory.Length());
		HistoryEntry entry = instance->pathForwardHistory.Pop();
		StringDestroy(&instance->delayedFocusItem);
		instance->delayedFocusItem = entry.focusedItem;
		InstanceLoadFolder(instance, entry.path, LOAD_FOLDER_FORWARD);
	}, stableCommandID++, "Alt+Right");

	EsCommandRegister(&instance->commandGoParent, instance, INTERFACE_STRING(FileManagerGoUp), [] (Instance *instance, EsElement *, EsCommand *) {
		String parent = PathGetParent(instance->folder->path);
		InstanceLoadFolder(instance, StringDuplicate(parent));
	}, stableCommandID++, "Alt+Up");

	EsCommandRegister(&instance->commandRefresh, instance, INTERFACE_STRING(FileManagerRefresh), [] (Instance *instance, EsElement *, EsCommand *) {
		FolderRefresh(instance->folder);
	}, stableCommandID++, "F5");

	EsCommandRegister(&instance->commandNewFolder, instance, INTERFACE_STRING(FileManagerNewFolderToolbarItem), CommandNewFolder, stableCommandID++, "Ctrl+Shift+N");
	EsCommandRegister(&instance->commandRename, instance, INTERFACE_STRING(FileManagerRenameAction), CommandRename, stableCommandID++, "F2");

	EsCommandRegister(&instance->commandViewDetails, instance, INTERFACE_STRING(CommonListViewTypeDetails), [] (Instance *instance, EsElement *, EsCommand *) {
		instance->viewSettings.viewType = VIEW_DETAILS;
		InstanceRefreshViewType(instance);
		InstanceViewSettingsUpdated(instance);
	}, stableCommandID++);

	EsCommandRegister(&instance->commandViewTiles, instance, INTERFACE_STRING(CommonListViewTypeTiles), [] (Instance *instance, EsElement *, EsCommand *) {
		instance->viewSettings.viewType = VIEW_TILES;
		InstanceRefreshViewType(instance);
		InstanceViewSettingsUpdated(instance);
	}, stableCommandID++);

	EsCommandRegister(&instance->commandViewThumbnails, instance, INTERFACE_STRING(CommonListViewTypeThumbnails), [] (Instance *instance, EsElement *, EsCommand *) {
		instance->viewSettings.viewType = VIEW_THUMBNAILS;
		InstanceRefreshViewType(instance);
		InstanceViewSettingsUpdated(instance);
	}, stableCommandID++);

	EsCommandSetDisabled(&instance->commandViewDetails, false);
	EsCommandSetDisabled(&instance->commandViewTiles, false);
	EsCommandSetDisabled(&instance->commandViewThumbnails, false);

	EsCommandSetCheck(&instance->commandViewDetails, ES_CHECK_CHECKED, false);
}
