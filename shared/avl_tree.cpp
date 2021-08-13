#ifndef IMPLEMENTATION

#ifdef DEBUG_BUILD
#define TREE_VALIDATE
#endif

#ifdef KERNEL
#define AVLPanic KernelPanic
#else
#define AVLPanic(...) do { EsPrint(__VA_ARGS__); EsAssert(false); } while (0)
#endif

enum TreeSearchMode {
	TREE_SEARCH_EXACT,
	TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL,
	TREE_SEARCH_LARGEST_BELOW_OR_EQUAL,
};

template <class T>
struct AVLTree;

struct AVLKey {
	union {
		uintptr_t shortKey;

		struct {
			void *longKey;
			size_t longKeyBytes;
		};
	};
};

inline AVLKey MakeShortKey(uintptr_t shortKey) {
	AVLKey key = {};
	key.shortKey = shortKey;
	return key;
}

inline AVLKey MakeLongKey(const void *longKey, size_t longKeyBytes) {
	AVLKey key = {};
	key.longKey = (void *) longKey;
	key.longKeyBytes = longKeyBytes;
	return key;
}

inline AVLKey MakeCStringKey(const char *cString) {
	return MakeLongKey(cString, EsCStringLength(cString) + 1);
}

template <class T>
struct AVLItem {
	T *thisItem;
	AVLItem<T> *children[2], *parent;
#ifdef TREE_VALIDATE
	AVLTree<T> *tree;
#endif
	AVLKey key;
	int height;
};

template <class T>
struct AVLTree {
	AVLItem<T> *root;
	bool modCheck;
	bool longKeys;
};

template <class T>
void TreeRelink(AVLItem<T> *item, AVLItem<T> *newLocation) {
	item->parent->children[item->parent->children[1] == item] = newLocation;
	if (item->children[0]) item->children[0]->parent = newLocation;
	if (item->children[1]) item->children[1]->parent = newLocation;
}

template <class T>
void TreeSwapItems(AVLItem<T> *a, AVLItem<T> *b) {
	// Set the parent of each item to point to the opposite one.
	a->parent->children[a->parent->children[1] == a] = b;
	b->parent->children[b->parent->children[1] == b] = a;

	// Swap the data between items.
	AVLItem<T> ta = *a, tb = *b;
	a->parent = tb.parent;
	b->parent = ta.parent;
	a->height = tb.height;
	b->height = ta.height;
	a->children[0] = tb.children[0];
	a->children[1] = tb.children[1];
	b->children[0] = ta.children[0];
	b->children[1] = ta.children[1];

	// Make all the children point to the correct item.
	if (a->children[0]) a->children[0]->parent = a; 
	if (a->children[1]) a->children[1]->parent = a; 
	if (b->children[0]) b->children[0]->parent = b;
	if (b->children[1]) b->children[1]->parent = b;
}

template <class T>
inline int TreeCompare(AVLTree<T> *tree, AVLKey *key1, AVLKey *key2) {
	if (tree->longKeys) {
		if (!key1->longKey && !key2->longKey) return 0;
		if (!key2->longKey) return  1;
		if (!key1->longKey) return -1;
		return EsStringCompareRaw((const char *) key1->longKey, key1->longKeyBytes, (const char *) key2->longKey, key2->longKeyBytes);
	} else {
		if (key1->shortKey < key2->shortKey) return -1;
		if (key1->shortKey > key2->shortKey) return  1;
		return 0;
	}
}

template <class T>
int TreeValidate(AVLItem<T> *root, bool before, AVLTree<T> *tree, AVLItem<T> *parent = nullptr, int depth = 0) {
#ifdef TREE_VALIDATE
	if (!root) return 0;
	if (root->parent != parent) AVLPanic("TreeValidate - Invalid binary tree 1 (%d).\n", before);
	if (root->tree != tree) AVLPanic("TreeValidate - Invalid binary tree 4 (%d).\n", before);

	AVLItem<T> *left  = root->children[0];
	AVLItem<T> *right = root->children[1];

	if (left  && TreeCompare(tree, &left->key,  &root->key) > 0) AVLPanic("TreeValidate - Invalid binary tree 2 (%d).\n", before);
	if (right && TreeCompare(tree, &right->key, &root->key) < 0) AVLPanic("TreeValidate - Invalid binary tree 3 (%d).\n", before);

	int leftHeight = TreeValidate(left, before, tree, root, depth + 1);
	int rightHeight = TreeValidate(right, before, tree, root, depth + 1);
	int height = (leftHeight > rightHeight ? leftHeight : rightHeight) + 1;
	if (height != root->height) AVLPanic("TreeValidate - Invalid AVL tree 1 (%d).\n", before);

#if 0
	static int maxSeenDepth = 0;
	if (maxSeenDepth < depth) {
		maxSeenDepth = depth;
		EsPrint("New depth reached! %d\n", maxSeenDepth);
	}
#endif

	return height;
#else
	(void) root;
	(void) before;
	(void) tree;
	(void) parent;
	(void) depth;
	return 0;
#endif
}

template <class T>
AVLItem<T> *TreeRotateLeft(AVLItem<T> *x) {
	AVLItem<T> *y = x->children[1], *t = y->children[0];
	y->children[0] = x, x->children[1] = t;
	if (x) x->parent = y; 
	if (t) t->parent = x;

	int leftHeight, rightHeight, balance;

	leftHeight  = x->children[0] ? x->children[0]->height : 0;
	rightHeight = x->children[1] ? x->children[1]->height : 0;
	balance     = leftHeight - rightHeight;
	x->height   = (balance > 0 ? leftHeight : rightHeight) + 1;

	leftHeight  = y->children[0] ? y->children[0]->height : 0;
	rightHeight = y->children[1] ? y->children[1]->height : 0;
	balance     = leftHeight - rightHeight;
	y->height   = (balance > 0 ? leftHeight : rightHeight) + 1;

	return y;
}

template <class T>
AVLItem<T> *TreeRotateRight(AVLItem<T> *y) {
	AVLItem<T> *x = y->children[0], *t = x->children[1];
	x->children[1] = y, y->children[0] = t;
	if (y) y->parent = x;
	if (t) t->parent = y;

	int leftHeight, rightHeight, balance;

	leftHeight  = y->children[0] ? y->children[0]->height : 0;
	rightHeight = y->children[1] ? y->children[1]->height : 0;
	balance     = leftHeight - rightHeight;
	y->height   = (balance > 0 ? leftHeight : rightHeight) + 1;

	leftHeight  = x->children[0] ? x->children[0]->height : 0;
	rightHeight = x->children[1] ? x->children[1]->height : 0;
	balance     = leftHeight - rightHeight;
	x->height   = (balance > 0 ? leftHeight : rightHeight) + 1;

	return x;
}

enum AVLDuplicateKeyPolicy {
	AVL_DUPLICATE_KEYS_PANIC,
	AVL_DUPLICATE_KEYS_ALLOW,
	AVL_DUPLICATE_KEYS_FAIL,
};

template <class T>
bool TreeInsert(AVLTree<T> *tree, AVLItem<T> *item, T *thisItem, AVLKey key, AVLDuplicateKeyPolicy duplicateKeyPolicy = AVL_DUPLICATE_KEYS_PANIC) {
	if (tree->modCheck) AVLPanic("TreeInsert - Concurrent modification\n");
	tree->modCheck = true; EsDefer({tree->modCheck = false;});

	TreeValidate(tree->root, true, tree);

#ifdef TREE_VALIDATE
	if (item->tree) {
		AVLPanic("TreeInsert - Item %x already in tree %x (adding to %x).\n", item, item->tree, tree);
	}

	item->tree = tree;
#endif

	item->key = key;
	item->children[0] = item->children[1] = nullptr;
	item->thisItem = thisItem;
	item->height = 1;

	AVLItem<T> **link = &tree->root, *parent = nullptr;

	while (true) {
		AVLItem<T> *node = *link;

		if (!node) {
			*link = item;
			item->parent = parent;
			break;
		}

		if (TreeCompare(tree, &item->key, &node->key) == 0) {
			if (duplicateKeyPolicy == AVL_DUPLICATE_KEYS_PANIC) {
				AVLPanic("TreeInsertRecursive - Duplicate keys: %x and %x both have key %x.\n", item, node, node->key);
			} else if (duplicateKeyPolicy == AVL_DUPLICATE_KEYS_FAIL) {
				return false;
			}
		}

		link = node->children + (TreeCompare(tree, &item->key, &node->key) > 0);
		parent = node;
	}

	AVLItem<T> fakeRoot = {};
	tree->root->parent = &fakeRoot;
#ifdef TREE_VALIDATE
	fakeRoot.tree = tree;
#endif
	fakeRoot.key = {};
	fakeRoot.children[0] = tree->root;

	item = item->parent;

	while (item != &fakeRoot) {
		int leftHeight  = item->children[0] ? item->children[0]->height : 0;
		int rightHeight = item->children[1] ? item->children[1]->height : 0;
		int balance = leftHeight - rightHeight;

		item->height = (balance > 0 ? leftHeight : rightHeight) + 1;
		AVLItem<T> *newRoot = nullptr;
		AVLItem<T> *oldParent = item->parent;

		if (balance > 1 && TreeCompare(tree, &key, &item->children[0]->key) <= 0) {
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateRight(item);
		} else if (balance > 1 && TreeCompare(tree, &key, &item->children[0]->key) > 0 && item->children[0]->children[1]) {
			item->children[0] = TreeRotateLeft(item->children[0]);
			item->children[0]->parent = item;
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateRight(item);
		} else if (balance < -1 && TreeCompare(tree, &key, &item->children[1]->key) > 0) {
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateLeft(item);
		} else if (balance < -1 && TreeCompare(tree, &key, &item->children[1]->key) <= 0 && item->children[1]->children[0]) {
			item->children[1] = TreeRotateRight(item->children[1]);
			item->children[1]->parent = item;
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateLeft(item);
		}

		if (newRoot) newRoot->parent = oldParent;
		item = oldParent;
	}

	tree->root = fakeRoot.children[0];
	tree->root->parent = nullptr;

	TreeValidate(tree->root, false, tree);
	return true;
}

template <class T>
AVLItem<T> *TreeFindRecursive(AVLTree<T> *tree, AVLItem<T> *root, AVLKey *key, TreeSearchMode mode) {
	if (!root) return nullptr;
	if (TreeCompare(tree, &root->key, key) == 0) return root;

	if (mode == TREE_SEARCH_EXACT) {
		return TreeFindRecursive(tree, root->children[TreeCompare(tree, &root->key, key) < 0], key, mode);
	} else if (mode == TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL) {
		if (TreeCompare(tree, &root->key, key) > 0) { 
			AVLItem<T> *item = TreeFindRecursive(tree, root->children[0], key, mode); 
			if (item) return item; else return root;
		} else { 
			return TreeFindRecursive(tree, root->children[1], key, mode); 
		}
	} else if (mode == TREE_SEARCH_LARGEST_BELOW_OR_EQUAL) {
		if (TreeCompare(tree, &root->key, key) < 0) { 
			AVLItem<T> *item = TreeFindRecursive(tree, root->children[1], key, mode); 
			if (item) return item; else return root;
		} else { 
			return TreeFindRecursive(tree, root->children[0], key, mode); 
		}
	} else {
		AVLPanic("TreeFindRecursive - Invalid search mode.\n");
		return nullptr;
	}
}

template <class T>
AVLItem<T> *TreeFind(AVLTree<T> *tree, AVLKey key, TreeSearchMode mode) {
	if (tree->modCheck) AVLPanic("TreeFind - Concurrent access\n");

	TreeValidate(tree->root, true, tree);
	return TreeFindRecursive(tree, tree->root, &key, mode);
}

template <class T>
int TreeGetBalance(AVLItem<T> *item) {
	if (!item) return 0;

	int leftHeight  = item->children[0] ? item->children[0]->height : 0;
	int rightHeight = item->children[1] ? item->children[1]->height : 0;
	return leftHeight - rightHeight;
}

template <class T>
void TreeRemove(AVLTree<T> *tree, AVLItem<T> *item) {
	if (tree->modCheck) AVLPanic("TreeRemove - Concurrent modification\n");
	tree->modCheck = true; EsDefer({tree->modCheck = false;});

	TreeValidate(tree->root, true, tree);

#ifdef TREE_VALIDATE
	if (item->tree != tree) AVLPanic("TreeRemove - Item %x not in tree %x (in %x).\n", item, tree, item->tree);
#endif

	AVLItem<T> fakeRoot = {};
	tree->root->parent = &fakeRoot;
#ifdef TREE_VALIDATE
	fakeRoot.tree = tree;
#endif
	fakeRoot.key = {}; 
	fakeRoot.children[0] = tree->root;

	if (item->children[0] && item->children[1]) {
		// Swap the item we're removing with the smallest item on its right side.
		AVLKey smallest = {};
		TreeSwapItems(TreeFindRecursive(tree, item->children[1], &smallest, TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL), item);
	}

	AVLItem<T> **link = item->parent->children + (item->parent->children[1] == item);
	*link = item->children[0] ? item->children[0] : item->children[1];
	if (*link) (*link)->parent = item->parent;
#ifdef TREE_VALIDATE
	item->tree = nullptr;
#endif
	if (*link) item = *link; else item = item->parent;

	while (item != &fakeRoot) {
		int leftHeight  = item->children[0] ? item->children[0]->height : 0;
		int rightHeight = item->children[1] ? item->children[1]->height : 0;
		int balance = leftHeight - rightHeight;

		item->height = (balance > 0 ? leftHeight : rightHeight) + 1;
		AVLItem<T> *newRoot = nullptr;
		AVLItem<T> *oldParent = item->parent;

		if (balance > 1 && TreeGetBalance(item->children[0]) >= 0) {
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateRight(item);
		} else if (balance > 1 && TreeGetBalance(item->children[0]) < 0) {
			item->children[0] = TreeRotateLeft(item->children[0]);
			item->children[0]->parent = item;
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateRight(item);
		} else if (balance < -1 && TreeGetBalance(item->children[1]) <= 0) {
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateLeft(item);
		} else if (balance < -1 && TreeGetBalance(item->children[1]) > 0) {
			item->children[1] = TreeRotateRight(item->children[1]);
			item->children[1]->parent = item;
			oldParent->children[oldParent->children[1] == item] = newRoot = TreeRotateLeft(item);
		}

		if (newRoot) newRoot->parent = oldParent;
		item = oldParent;
	}

	tree->root = fakeRoot.children[0];

	if (tree->root) {
		if (tree->root->parent != &fakeRoot) AVLPanic("TreeRemove - Incorrect root parent.\n");
		tree->root->parent = nullptr;
	}

	TreeValidate(tree->root, false, tree);
}

#endif
