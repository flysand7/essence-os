# Debugging

This file has not been written yet. `Notes from Discord.txt` may contain a few unorganized pointers, though.

TODO

## GDB commands

Here are various commands for GDB for inspecting kernel state. Put these in your `.gdbinit` file if you want to use them. *Please note that some of these may be out of date.*

The `ldcx` command is used to load the RIP, RSP and RBP registers from an `InterruptContext`.

	define ldcx
		set $new_rip = context->rip
		set $new_rsp = context->rsp
		set $new_rbp = context->rbp
		set $rip = $new_rip
		set $rsp = $new_rsp
		set $rbp = $new_rbp
		set $rip = $new_rip
	end

	define ldcx32
		set $new_eip = context->eip
		set $new_esp = context->esp
		set $new_ebp = context->ebp
		set $eip = $new_eip
		set $esp = $new_esp
		set $ebp = $new_ebp
		set $eip = $new_eip
	end

The `print_page_list` command follows and prints a linked list from the physical memory manager's page frame database.

	define print_page_list
		set $index = $arg0
		while $index
			set $page_frame = pmm.pageFrames[$index]
			p $index
			p $page_frame
			set $index = $page_frame.list.next
		end
	end

Here are some Python commands. `PrintKernelMMSpaceRegions` prints the memory regions in the kernel's memory space. `PrintDeviceTree` prints all devices in the kernel's device tree, including the number of open reference to each device. `PrintBootFileSystemNodes` prints all the loaded nodes in the boot filesystem ("0:/"). `PrintCachedNodes` prints all the nodes that are in cached but have no open references. This also installs hooks for the gf GDB frontend watch window for `EsRectangle` and `Array<T>`.

	py

	def EsRectangleHook(item, field):
		if field:
			if field == '[width]':  return gdb.Value(int(item['r']) - item['l'])
			if field == '[height]': return gdb.Value(int(item['b']) - item['t'])
		else:
			print('[width]')
			print('[height]')
			_gf_fields_recurse(item)

	def EsArrayHook(item, field):
		if field:
			return item['array'][int(field[1:-1])]
		else:
			print('(d_arr)',ArrayLength(item['array']))

	def ArrayLength(arrayBase):
		if int(arrayBase):
			return int(gdb.parse_and_eval('(_ArrayHeader*)' + str(int(arrayBase)) + '-1')['length'])
		return 0

	def PrintAVLKey(key, end='\n'):
		charPointer = gdb.lookup_type('char').pointer()
		nameBytes = int(key['longKeyBytes'])
		name = key['longKey'].cast(charPointer)
		print(name.string('utf-8', 'strict', nameBytes), end=end)

	def PrintNodePath(node, end='\n'):
		if isinstance(node, str):
			node = gdb.parse_and_eval(node)
		parent = node['directoryEntry']['parent']
		if int(parent):
			PrintNodePath(parent, end='/')
		key = node['directoryEntry']['item']['key']
		PrintAVLKey(key, end)

	def PrintCachedNodes():
		item = gdb.parse_and_eval('fs.cachedNodes.firstItem')
		offset = int(gdb.parse_and_eval('(uintptr_t)(&((KNode*)0)->cacheItem)-(uintptr_t)((KNode*)0)'))
		while int(item):
			node = gdb.parse_and_eval('(KNode*)' + str(int(item) - offset))
			PrintNodePath(node)
			item = item['nextItem']

	def PrintDirectoryEntryAndChildren(directoryEntry, indent=''):
		if isinstance(directoryEntry, str):
			directoryEntry = gdb.parse_and_eval(directoryEntry)
		print('"', end='')
		PrintAVLKey(directoryEntry['item']['key'], end='"')
		node = directoryEntry['node']
		recurse = None
		if int(node):
			print(', node H' + str(int(node['handles'])), end='')
			if int(directoryEntry['type']) == 0x10:
				print(', dir', end='')
				fsDirectory = node.cast(gdb.lookup_type('FSDirectory').pointer())
				recurse = fsDirectory['entries']['root']
		print('')
		if recurse:
			RecurseIntoNodeAVLTree(recurse, indent + '\t')
		
	def RecurseIntoNodeAVLTree(item, indent):
		if not int(item):
			return
		RecurseIntoNodeAVLTree(item['children'][0], indent)
		RecurseIntoNodeAVLTree(item['children'][1], indent)
		print(indent, end='')
		PrintDirectoryEntryAndChildren(item['thisItem'], indent)
		
	def PrintBootFileSystemNodes():
		PrintDirectoryEntryAndChildren('fs.bootFileSystem.rootDirectory.directoryEntry')

	def RecurseIntoDeviceTree(device, indent):
		print(indent, device['cDebugName'].string('utf-8'), device['handles'])
		childCount = ArrayLength(device['children']['array'])
		for i in range(childCount):
			RecurseIntoDeviceTree(device['children']['array'][i], indent + ' | ')

	def PrintDeviceTree():
		RecurseIntoDeviceTree(gdb.parse_and_eval('deviceTreeRoot'), '')

	def PrintMemoryRegion(region):
		print('region: ' + hex(int(region)))
		print('\tbaseAddress: ' + hex(int(region['baseAddress'])))
		print('\tsize: ' + str(4 * int(region['pageCount'])) + ' KB')
		flags = int(region['flags'])
		print('\tflags: ' + hex(flags))
		if flags & 0x200:
			print('\tnormal region')
			print('\t\tcommit: ' + str(4 * int(region['data']['normal']['commitPageCount'])) + ' KB')
		
	def RecurseIntoMMSpaceAVLTree(item):
		if not int(item):
			return
		RecurseIntoMMSpaceAVLTree(item['children'][0])
		RecurseIntoMMSpaceAVLTree(item['children'][1])
		PrintMemoryRegion(item['thisItem'])
		
	def PrintKernelMMSpaceRegions():
		RecurseIntoMMSpaceAVLTree(gdb.parse_and_eval('_kernelMMSpace.usedRegions.root'))

	gf_hooks = { 'EsRectangle': EsRectangleHook, 'Array': EsArrayHook }

	end
