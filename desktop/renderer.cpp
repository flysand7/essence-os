// TODO Fix glitches.
// TODO RAST_REPEAT_NORMAL is wrong with negative values.

#ifdef IN_DESIGNER
#define RAST_ARRAY(x) x *
#define RAST_ARRAY_ADD arrput
#define RAST_ARRAY_CLEAR arrclear
#define RAST_ARRAY_DELETE_SWAP arrdelswap
#define RAST_ARRAY_FREE arrfree
#define RAST_ARRAY_INSERT arrinsn
#define RAST_ARRAY_LAST arrlast
#define RAST_ARRAY_LENGTH arrlen
#define RAST_ARRAY_LENGTH_U arrlenu
#else
#define RAST_ARRAY(x) Array<x>
#define RAST_ARRAY_ADD(x, y) ((x).Add(y))
#define RAST_ARRAY_CLEAR(x) ((x).SetLength(0))
#define RAST_ARRAY_DELETE_SWAP(x, y) ((x).DeleteSwap(y))
#define RAST_ARRAY_FREE(x) ((x).Free())
#define RAST_ARRAY_INSERT(x, y, z) ((x).InsertMany(y, z))
#define RAST_ARRAY_LAST(x) ((x).Last())
#define RAST_ARRAY_LENGTH(x) ((intptr_t) (x).Length())
#define RAST_ARRAY_LENGTH_U(x) ((x).Length())
#endif

typedef struct RastVertex {
	float x, y;
} RastVertex;

typedef struct RastEdge {
	float xf, yf, xt, yt;
	float dx, dy;
	int sign;
} RastEdge;

typedef struct RastShape {
	RAST_ARRAY(RastEdge) edges; /* sorted by yf; yf <= yt */
	int left, right, top, bottom;
} RastShape;

typedef struct RastSurface {
	uint32_t *buffer;
	int width, height, stride;
	float *area, *areaFill;
	bool customBuffer;
} RastSurface;

typedef enum RastPaintType {
	RAST_PAINT_NONE,
	RAST_PAINT_SOLID,
	RAST_PAINT_CHECKERBOARD,
	RAST_PAINT_LINEAR_GRADIENT,
	RAST_PAINT_RADIAL_GRADIENT,
	RAST_PAINT_ANGULAR_GRADIENT,
	RAST_PAINT_NOISE,
} RastPaintType;

typedef enum RastRepeatMode {
	RAST_REPEAT_CLAMP,
	RAST_REPEAT_NORMAL,
	RAST_REPEAT_MIRROR,
} RastRepeatMode;

typedef struct RastPaint {
	RastPaintType type;
	
	union {
		struct {
			uint32_t color;
			float alpha;
		} solid;
		
		struct {
			uint32_t color1, color2;
			float alpha1, alpha2;
			int size;
		} checkboard;
		
		struct {
			uint32_t *color;
			float *alpha;
			float transform[6];
			RastRepeatMode repeatMode;
		} gradient;
		
		struct {
			uint32_t color;
			float minimum, maximum;
		} noise;
	};
} RastPaint;

typedef struct RastGradientStop {
	uint32_t color;
	float position;
} RastGradientStop;

typedef struct RastPathSegment {
	uintptr_t uptoVertex;
} RastPathSegment;

typedef struct RastPath {
	RAST_ARRAY(RastPathSegment) segments;
	RAST_ARRAY(RastVertex) vertices;
} RastPath;

typedef enum RastLineJoinMode {
	// Determines how convex segments are joined.
	// Concave segments are always mitered with infinite limit.
	
	RAST_LINE_JOIN_MITER, // Force bevels with miterLimit = 0.
	RAST_LINE_JOIN_ROUND, 
} RastLineJoinMode;

typedef enum RastLineCapMode {
	RAST_LINE_CAP_FLAT,
	RAST_LINE_CAP_SQUARE,
	RAST_LINE_CAP_ROUND,
} RastLineCapMode;

typedef struct RastContourStyle {
	float internalWidth, externalWidth;
	RastLineJoinMode joinMode;
	float miterLimit;
	RastLineCapMode capMode;
	// TODO Markers.
} RastContourStyle;

typedef struct RastDash {
	float length, gap;
	RastContourStyle *style;
} RastDash;

#define RAST_ADD_VERTEX_MINIMUM_DISTANCE_SQUARED (0.1f * 0.1f)
#define RAST_GRADIENT_COLORS (256)
#define RAST_ROUND_TOLERANCE (0.25f)
#define RAST_FLATTEN_TOLERANCE (0.25f)
#define RAST_GRADIENT_NOISE (0.005f)
	
#define RAST_AVERAGE_VERTICES(a, b) { ((a).x + (b).x) * 0.5f, ((a).y + (b).y) * 0.5f }

bool RastSurfaceInitialise(RastSurface *surface, int width, int height, bool customBuffer) {
	surface->width = width;
	surface->height = height;
	surface->stride = 4 * width;
	surface->area = (float *) EsHeapAllocate(sizeof(float) * width, true);
	surface->areaFill = (float *) EsHeapAllocate(sizeof(float) * (width + 1), true);
	surface->customBuffer = customBuffer;

	if (!customBuffer) {
		surface->buffer = (uint32_t *) EsHeapAllocate(4 * width * height, true);
	}

	return surface->buffer && surface->area && surface->areaFill;
}

void RastSurfaceDestroy(RastSurface *surface) {
	EsHeapFree(surface->area);
	EsHeapFree(surface->areaFill);

	if (!surface->customBuffer) {
		EsHeapFree(surface->buffer);
	}
}

#ifndef IN_DESIGNER
ES_MACRO_SORT(RastEdgesSort, RastEdge, { result = _left->yf > _right->yf ? 1 : _left->yf < _right->yf ? -1 : 0; }, void *);
#else
int _RastEdgeCompare(const void *left, const void *right) {
	const RastEdge *_left = (const RastEdge *) left;
	const RastEdge *_right = (const RastEdge *) right;
	return _left->yf > _right->yf ? 1 : _left->yf < _right->yf ? -1 : 0;
}

void RastEdgesSort(RastEdge *edges, size_t count, void *_unused) {
	(void) _unused;
	qsort(edges, count, sizeof(RastEdge), _RastEdgeCompare);
}
#endif

float _RastRepeat(RastRepeatMode mode, float p) {
	if (mode == RAST_REPEAT_CLAMP) {
		if (p < 0) return 0;
		if (p > 1) return 1;
		return p;
	} else if (mode == RAST_REPEAT_MIRROR) {
		p = EsCRTfabsf(EsCRTfmodf(p, 2.0f));
		if (p > 1) return 2 - p;
		return p;
	} else {
		return EsCRTfabsf(EsCRTfmodf(p, 1.0f));
	}
}

void _RastShapeDestroy(RastShape shape) {
	RAST_ARRAY_FREE(shape.edges);
}

void RastSurfaceFill(RastSurface surface, RastShape shape, RastPaint paint, bool evenOdd) {
	if (paint.type == RAST_PAINT_LINEAR_GRADIENT 
			|| paint.type == RAST_PAINT_RADIAL_GRADIENT
			|| paint.type == RAST_PAINT_ANGULAR_GRADIENT) {
		if (!paint.gradient.color) {
			_RastShapeDestroy(shape);
			return;
		}
	}

	RAST_ARRAY(RastEdge) active = { 0 };
	int edgePosition = 0;
	
	if (shape.left < 0) shape.left = 0;
	if (shape.right > surface.width) shape.right = surface.width;
	if (shape.top < 0) shape.top = 0;
	if (shape.bottom > surface.height) shape.bottom = surface.height;

	// Split edges that cross the left side of the shape.

	int initialShapeEdges = RAST_ARRAY_LENGTH(shape.edges);

	for (int i = 0; i < initialShapeEdges; i++) {
		RastEdge *a = &shape.edges[i];

		float y0 = a->yf;
		float y1 = a->yt;
		float x0 = a->xf;
		float x1 = a->xt;
		bool flipped = false;

		if (x0 > x1) {
			float t = x0;
			x0 = x1, x1 = t;
			flipped = true;
		}

		if (x0 < shape.left && x1 >= shape.left) {
			RastEdge e = *a;

			if (flipped) {
				y1 += (shape.left - x0) * a->dy;
				a->xt = e.xf = e.xt = shape.left;
				e.yf = a->yt = y1;
				e.dx = 0;
			} else {
				y0 += (shape.left - x0) * a->dy;
				a->xt = e.xf = a->xf = shape.left;
				e.yf = a->yt = y0;
				a->dx = 0;
			}

			RAST_ARRAY_ADD(shape.edges, e);
		}
	}

	RastEdgesSort(&shape.edges[0], RAST_ARRAY_LENGTH(shape.edges), NULL);
	
	if (paint.type == RAST_PAINT_CHECKERBOARD && paint.checkboard.size < 1) paint.checkboard.size = 1;
	
	for (int scanline = shape.top; scanline < shape.bottom; scanline++) {
		// Remove edges above this scanline.
		
		for (int i = 0; i < RAST_ARRAY_LENGTH(active); i++) {
			if (active[i].yt < scanline) {
				RAST_ARRAY_DELETE_SWAP(active, i);
				i--;
			}
		}
		
		// Add edges that start within this scanline.
		
		while (edgePosition < RAST_ARRAY_LENGTH(shape.edges)) {
			RastEdge *e = &shape.edges[edgePosition];
			
			if (e->yf < scanline + 1.0f) {
				RAST_ARRAY_ADD(active, *e);
				edgePosition++;
			} else {
				break;
			}
		}
		
		// If there are no active edges, don't process the scanline.
		
		if (!RAST_ARRAY_LENGTH(active)) {
			continue;
		}
		
		// Calculate the signed area covered by each active edge.
		
		for (int i = 0; i < RAST_ARRAY_LENGTH(active); i++) {
			RastEdge *a = &active[i];
			
			// Calculate the range of pixels the edge crosses on the scanline.
			
			float top = scanline, bottom = scanline + 1;
			float y0 = (a->yf > top) ? a->yf : top;
			float y1 = (a->yt < bottom) ? a->yt : bottom;
			float x0 = (y0 - a->yf) * a->dx + a->xf;
			float x1 = (y1 - a->yf) * a->dx + a->xf;
			float dy = a->dy;
			bool flipped = false;

			if (x1 < x0) { 
				// Convert NE-SW edge to NW-SE.
				// Flipping the edge preserves signed area.
				
				float t = y0;
				y0 = top + bottom - y1;
				y1 = top + bottom - t;
				t = x0, x0 = x1, x1 = t;
				dy = -dy;
				flipped = true;
			}

			if (x1 < shape.left) {
				x0 = x1 = shape.left;
			} else if (x0 < shape.left) {
				y0 += (shape.left - x0) * dy;
				x0 = shape.left;
			}

			if (x1 >= shape.right) {
				y1 += (x1 - shape.right + 1) * dy;
				x1 = shape.right - 1;
			}

			if (y1 <= y0 || x0 >= shape.right || x1 < shape.left || EsCRTisnanf(x0) || EsCRTisnanf(x1) || EsCRTisnanf(y0) || EsCRTisnanf(y1)) {
				continue;
			}
			
			if (EsCRTfloorf(x0) == EsCRTfloorf(x1)) { 
				// Edge crosses one pixel on this scanline,
				// forming a trapezium.
				
				float right = EsCRTfloorf(x0 + 1);
				float p = a->sign * (y1 - y0);
				int xs = (int) x0;
				surface.area[xs] += p * (right - x0 + right - x1) * 0.5f;
				surface.areaFill[xs + 1] += p;
			} else { 
				// Edge crosses multiple pixels on this scanline.
				// The first pixel is a triangle.
				
				float tx = EsCRTfloorf(x0 + 1);
				float th = dy * (tx - x0);
				int xs = (int) x0, xf = (int) x1;
				surface.area[xs] += a->sign * (tx - x0) * th * 0.5f;
				
				// The middle pixels are trapeziums.
				
				float pa = th + 0.5f * dy;
				for (int j = xs + 1; j < xf; j++, pa += dy) surface.area[j] += a->sign * pa;
				
				// The final pixel is a removed triangle.
				
				float p = a->sign * (y1 - y0);
				float fx = EsCRTfloorf(x1);
				float fy = a->yf + a->dy * (fx - a->xf);
				if (flipped) fy = top + bottom - fy;
				surface.area[xf] += p - a->sign * 0.5f * (x1 - fx) * (y1 - fy);
				surface.areaFill[xf + 1] += p;
			}
		}
		
		// Calculate the final coverage of each pixel.
		
		float cumulativeArea = 0;
		uint32_t *destination = (uint32_t *) ((uint8_t *) surface.buffer + scanline * surface.stride + 4 * shape.left);
		
		float textureDX = paint.gradient.transform[0];
		float texturePX = shape.left * textureDX + (float) scanline * paint.gradient.transform[1] + paint.gradient.transform[2];
		float textureDY = paint.gradient.transform[3];
		float texturePY = shape.left * textureDY + (float) scanline * paint.gradient.transform[4] + paint.gradient.transform[5];

		if (paint.type == RAST_PAINT_NOISE) {
			textureDX = 1;
			texturePX = 0;
		}
		
		for (int i = shape.left; i < shape.right; i++) {
			cumulativeArea += surface.areaFill[i];
			float a = surface.area[i] + cumulativeArea;

			if (evenOdd) {
				a = EsCRTfmodf(AbsoluteFloat(a), 2);
				if (a > 1) a = 2 - a;
			} else {
				if (a < 0) a = -a;
				if (a > 1) a = 1;
			}
			
			if (a > 0.0039f) {
				if (paint.type == RAST_PAINT_SOLID) {
					uint8_t c = (uint8_t) (a * paint.solid.alpha * 255.0f);
					if (c) BlendPixel(destination, (c << 24) | paint.solid.color, true);
				} else if (paint.type == RAST_PAINT_CHECKERBOARD) {
					if (((i - shape.left) / paint.checkboard.size + (scanline - shape.top) / paint.checkboard.size) & 1) {
						uint8_t c = (uint8_t) (a * paint.checkboard.alpha2 * 255.0f);
						if (c) BlendPixel(destination, (c << 24) | paint.checkboard.color2, true);
					} else {
						uint8_t c = (uint8_t) (a * paint.checkboard.alpha1 * 255.0f);
						if (c) BlendPixel(destination, (c << 24) | paint.checkboard.color1, true);
					}
				} else if (paint.type == RAST_PAINT_LINEAR_GRADIENT) {
					float p = _RastRepeat(paint.gradient.repeatMode, texturePX);
					int pi = (int) ((RAST_GRADIENT_COLORS - 1) * p);
					EsAssert(pi >= 0 && pi < RAST_GRADIENT_COLORS); // Invalid gradient index.
					uint8_t c = (uint8_t) (a * paint.gradient.alpha[pi] * 255.0f);
					if (c) BlendPixel(destination, (c << 24) | paint.gradient.color[pi], true);
				} else if (paint.type == RAST_PAINT_RADIAL_GRADIENT) {
					float p = EsCRTsqrtf(texturePX * texturePX + texturePY * texturePY);
					if (p < 0) p = 0;
					if (p > 1) p = 1;
					int pi = (int) ((RAST_GRADIENT_COLORS - 1) * p);
					uint8_t c = (uint8_t) (a * paint.gradient.alpha[pi] * 255.0f);
					if (c) BlendPixel(destination, (c << 24) | paint.gradient.color[pi], true);
				} else if (paint.type == RAST_PAINT_ANGULAR_GRADIENT) {
					float p = EsCRTatan2f(texturePY, texturePX) * 0.159154943091f + 0.5f;
					if (p < 0) p = 0;
					if (p > 1) p = 1;
					int pi = (int) ((RAST_GRADIENT_COLORS - 1) * p);
					uint8_t c = (uint8_t) (a * paint.gradient.alpha[pi] * 255.0f);
					if (c) BlendPixel(destination, (c << 24) | paint.gradient.color[pi], true);
				} else if (paint.type == RAST_PAINT_NOISE) {
					union { float f; uint32_t u; } noise = { texturePX + texturePY };
					noise.u += noise.u << 10;
					noise.u ^= noise.u >> 6;
					noise.u += noise.u << 3;
					noise.u ^= noise.u >> 11;
					noise.u += noise.u << 15;
					noise.u &= 0x7FFFFF;
					noise.u |= 0x3F800000;
					noise.f /= 2;
					noise.f *= paint.noise.maximum - paint.noise.minimum;
					noise.f += paint.noise.minimum;
					
					uint8_t c = (uint8_t) (a * noise.f * 255.0f);
					if (c) BlendPixel(destination, (c << 24) | paint.noise.color, true);
				}
			}
			
			surface.area[i] = surface.areaFill[i] = 0;
			destination++;
			texturePX += textureDX;
			texturePY += textureDY;
		}

		surface.areaFill[shape.right] = 0;
	}
	
	RAST_ARRAY_FREE(active);
	_RastShapeDestroy(shape);
}

void _RastShapeAddEdges(RastShape *shape, RastVertex *vertices, int sign, bool open, int vertexCount) {
	if (vertexCount <= 1) return;
	
	for (int i = 0; i < vertexCount - (open ? 1 : 0); i++) {
		RastVertex *from = &vertices[i];
		RastVertex *to = &vertices[(i + 1) % vertexCount];
		if (from->y == to->y) continue;
		
		int p = RAST_ARRAY_LENGTH(shape->edges);
		RAST_ARRAY_INSERT(shape->edges, p, 1);
		RastEdge *edge = &shape->edges[p];
		
		if (from->y < to->y) {
			edge->xf = from->x, edge->yf = from->y;
			edge->xt = to->x, edge->yt = to->y;
			edge->sign = sign;
		} else {
			edge->xf = to->x, edge->yf = to->y;
			edge->xt = from->x, edge->yt = from->y;
			edge->sign = -sign;
		}
		
		edge->dx = (edge->xt - edge->xf) / (edge->yt - edge->yf);
		edge->dy = 1.0f / edge->dx;
		
		if (edge->xf < shape->left) shape->left = edge->xf;
		if (edge->xt < shape->left) shape->left = edge->xt;
		if (edge->yf < shape->top)  shape->top  = edge->yf;
		if (edge->xf + 1 > shape->right)  shape->right  = edge->xf + 1;
		if (edge->xt + 1 > shape->right)  shape->right  = edge->xt + 1;
		if (edge->yt + 1 > shape->bottom) shape->bottom = edge->yt + 1;
	}
}

void RastPathCloseSegment(RastPath *path) {
	if (!RAST_ARRAY_LENGTH_U(path->segments) || RAST_ARRAY_LAST(path->segments).uptoVertex != RAST_ARRAY_LENGTH_U(path->vertices)) {
		RastPathSegment segment = {};
		segment.uptoVertex = RAST_ARRAY_LENGTH_U(path->vertices);
		RAST_ARRAY_ADD(path->segments, segment);
	}
}

RastShape RastShapeCreateSolid(RastPath *path) {
	if (RAST_ARRAY_LENGTH(path->vertices) < 3) return (RastShape) { 0 };
	RastPathCloseSegment(path);
	RastShape shape = { .left = INT_MAX, .top = INT_MAX };
	uintptr_t start = 0;

	for (uintptr_t i = 0; i < RAST_ARRAY_LENGTH_U(path->segments); i++) {
		_RastShapeAddEdges(&shape, &path->vertices[start], 1, false, path->segments[i].uptoVertex - start);
		start = path->segments[i].uptoVertex;
	}

	return shape;
}

void _RastJoinMeter(RAST_ARRAY(RastVertex) *output, RastVertex *from1, RastVertex *to1, RastVertex *from2, RastVertex *to2, RastVertex *point, float miterLimit) {
	// TODO For internal contours, this can generate vertices forming anticlockwise regions if the contour width is large enough.
	//	(You can't see it though because we use a non-zero fill rule.)
	
	float a = to1->x - from1->x, b = from2->x - to2->x, e = from2->x - from1->x;
	float c = to1->y - from1->y, d = from2->y - to2->y, f = from2->y - from1->y;
	float j = (d * e - b * f) / (d * a - b * c);
	RastVertex v = { from1->x + j * (to1->x - from1->x), from1->y + j * (to1->y - from1->y) };
	
	if ((v.x - point->x) * (v.x - point->x) + (v.y - point->y) * (v.y - point->y) <= miterLimit * miterLimit) {
		RAST_ARRAY_ADD(*output, v);
	} else {
		// TODO Move bevel to miter limit.
		RAST_ARRAY_ADD(*output, *to1);
		RAST_ARRAY_ADD(*output, *from2);
	}
}

void _RastJoinArc(RAST_ARRAY(RastVertex) *output, float fromAngle, float toAngle, int divisions, RastVertex *point, float width) {
	for (int j = 0; j < divisions; j++) {
		float angle = fromAngle + j * (toAngle - fromAngle) / (divisions - 1);
		RastVertex v = { point->x + EsCRTcosf(angle) * width, point->y + EsCRTsinf(angle) * width };
		
		if (j == 0 || j == divisions - 1) {
			RAST_ARRAY_ADD(*output, v);
			continue;
		}
			
		RastVertex l = RAST_ARRAY_LAST(*output);
		int dx = v.x - l.x, dy = v.y - l.y;
		
		if (dx * dx + dy * dy > RAST_ADD_VERTEX_MINIMUM_DISTANCE_SQUARED) {
			RAST_ARRAY_ADD(*output, v);
		}
	}
}

void _RastJoinRound(RAST_ARRAY(RastVertex) *output, RastVertex *from1, RastVertex *to1, RastVertex *from2, 
		RastVertex *to2, RastVertex *point, float width, int divisions, bool internal) {
	float fromAngle = EsCRTatan2f(to1->y - point->y, to1->x - point->x);
	float toAngle = EsCRTatan2f(from2->y - point->y, from2->x - point->x);
	
	if (toAngle > fromAngle) {
		toAngle -= 6.2831853071f;
	}

	if (internal == (fromAngle - toAngle < 3.141592653f)) {
		_RastJoinMeter(output, from1, to1, from2, to2, point, ES_INFINITY);
		return;
	}
	
	if (internal) {
		toAngle += 6.2831853071f;
	}
	
	_RastJoinArc(output, fromAngle, toAngle, divisions, point, width);
}

void _RastShapeCreateContour(RastShape *shape, RastPath *path, RastContourStyle style, bool open) {
	RAST_ARRAY(RastVertex) vertices = path->vertices;
	size_t vertexCount = RAST_ARRAY_LENGTH(vertices);

	if (vertexCount < 2) {
		return;
	}

	if (!open) {
		RastVertex first = vertices[0], last = RAST_ARRAY_LAST(vertices);
		float dx = first.x - last.x, dy = first.y - last.y;

		if (dx * dx + dy * dy < RAST_ADD_VERTEX_MINIMUM_DISTANCE_SQUARED) {
			vertexCount--;
		}
	}

	if (vertexCount <= 1) {
		return;
	} else if (vertexCount == 2) {
		open = true;
	}
	
	RastVertex cap1, cap2, cap3, cap4;
		
	RAST_ARRAY(RastVertex) path1 = { 0 };
	RAST_ARRAY(RastVertex) path2 = { 0 };
	
	for (int internal = 0; internal < 2; internal++) {
		float width = internal ? style.internalWidth : style.externalWidth;
		RastVertex *capStart = internal ? &cap4 : &cap1;
		RastVertex *capEnd = internal ? &cap3 : &cap2;
		
		for (uintptr_t i = 0; i < vertexCount; i++) {
			RastVertex *from = &vertices[(i + 0) % vertexCount], 
				   *to   = &vertices[(i + 1) % vertexCount];
			float dx = to->x - from->x, dy = to->y - from->y;
			float scale = (internal ? -width : width) / EsCRTsqrtf(dx * dx + dy * dy);
			float ox = -dy * scale, oy = dx * scale;
			RastVertex cf = { from->x + ox, from->y + oy };
			RastVertex ct = { to->x + ox, to->y + oy };
			RAST_ARRAY_ADD(path1, cf);
			RAST_ARRAY_ADD(path1, ct);
		}
		
		if (open) RAST_ARRAY_ADD(path2, (*capStart = path1[0]));
		
		if (style.joinMode == RAST_LINE_JOIN_MITER) {
			for (int i = 0; i < RAST_ARRAY_LENGTH(path1) - (open ? 4 : 0); i += 2) {
				RastVertex *p1 = &vertices[(i / 2 + 0) % vertexCount],
					*p2 = &vertices[(i / 2 + 1) % vertexCount],
					*p3 = &vertices[(i / 2 + 2) % vertexCount];
				float cross = (p2->x - p1->x) * (p3->y - p2->y) - (p2->y - p1->y) * (p3->x - p2->x);
				
				_RastJoinMeter(&path2, &path1[i], &path1[i + 1], &path1[(i + 2) % RAST_ARRAY_LENGTH(path1)], 
					&path1[(i + 3) % RAST_ARRAY_LENGTH(path1)], &vertices[(i / 2 + 1) % vertexCount], 
					(internal ? (cross > 0) : (cross < 0)) ? style.miterLimit : ES_INFINITY);
			}
		} else if (style.joinMode == RAST_LINE_JOIN_ROUND) {
			int divisions = EsCRTceilf(3.14159265358f * 0.5f / EsCRTacosf(width / (width + RAST_ROUND_TOLERANCE))) + 1;
				
			for (int i = 0; i < RAST_ARRAY_LENGTH(path1) - (open ? 4 : 0); i += 2) {
				_RastJoinRound(&path2, &path1[i], &path1[i + 1], &path1[(i + 2) % RAST_ARRAY_LENGTH(path1)], 
					&path1[(i + 3) % RAST_ARRAY_LENGTH(path1)], &vertices[(i / 2 + 1) % vertexCount], width, divisions, internal);
			}
		}
		
		if (open) RAST_ARRAY_ADD(path2, (*capEnd = path1[RAST_ARRAY_LENGTH(path1) - 3]));
		
		_RastShapeAddEdges(shape, &path2[0], internal ? -1 : 1, open, RAST_ARRAY_LENGTH(path2));
			
		RAST_ARRAY_CLEAR(path1);
		RAST_ARRAY_CLEAR(path2);
	}
	
	if (open) {
		// TODO Cap styles don't work if the contour is not centered (i.e. internalWidth != externalWidth).

		for (int i = 0; i < 2; i++) {
			RastVertex c = i ? cap4 : cap2, d = i ? cap1 : cap3;
			RastVertex *from = &vertices[(i ? 1 : (vertexCount - 2))];
			RastVertex *to = &vertices[(i ? 0 : (vertexCount - 1))];
			RAST_ARRAY_ADD(path1, c);
			
			if (style.capMode == RAST_LINE_CAP_SQUARE) {
				float dx = to->x - from->x, dy = to->y - from->y;
				float scale = 0.5f * (style.internalWidth + style.externalWidth) / EsCRTsqrtf(dx * dx + dy * dy);
				RastVertex c0 = { .x = c.x + scale * dx, .y = c.y + scale * dy };
				RastVertex d0 = { .x = d.x + scale * dx, .y = d.y + scale * dy };
				RAST_ARRAY_ADD(path1, c0);
				RAST_ARRAY_ADD(path1, d0);
			} else if (style.capMode == RAST_LINE_CAP_ROUND) {
				float angle = EsCRTatan2f(d.y - to->y, d.x - to->x);				
				float width = 0.5f * (style.internalWidth + style.externalWidth);
				int divisions = EsCRTceilf(3.14159265358f * 0.5f / EsCRTacosf(width / (width + RAST_ROUND_TOLERANCE))) + 1;
				_RastJoinArc(&path1, angle + 3.14159265358f, angle, divisions, to, width);
			}
			
			RAST_ARRAY_ADD(path1, d);
			_RastShapeAddEdges(shape, &path1[0], 1, true, RAST_ARRAY_LENGTH(path1));
			RAST_ARRAY_CLEAR(path1);
		}
	}
		
	RAST_ARRAY_FREE(path1);
	RAST_ARRAY_FREE(path2);
}

RastShape RastShapeCreateContour(RastPath *path, RastContourStyle style, bool open) {
	RastShape shape = { .left = INT_MAX, .top = INT_MAX };
	_RastShapeCreateContour(&shape, path, style, open);
	if (RAST_ARRAY_LENGTH(shape.edges) == 0) return (RastShape) { 0 };
	return shape;
}

void _RastPathAddVertex(RastPath *path, RastVertex vertex) {
	if (RAST_ARRAY_LENGTH(path->vertices)) {
		RastVertex last = RAST_ARRAY_LAST(path->vertices);
		float dx = last.x - vertex.x, dy = last.y - vertex.y;
	
		if (dx * dx + dy * dy < RAST_ADD_VERTEX_MINIMUM_DISTANCE_SQUARED) {
			return;
		}
	}
	
	RAST_ARRAY_ADD(path->vertices, vertex);
}

RastShape RastShapeCreateDashed(RastPath *path, RastDash *dashStyles, size_t dashStyleCount, bool open) {
	RAST_ARRAY(RastVertex) vertices = path->vertices;
	size_t vertexCount = RAST_ARRAY_LENGTH(vertices);

	if (dashStyleCount < 1 || vertexCount < 2) {
		return (RastShape) { 0 };
	}

	RastDash *style = dashStyles + 0;
	RastVertex from = vertices[0];
	RastVertex *to = &vertices[1];
	RastPath dash = {};
	RastShape shape = { .left = INT_MAX, .top = INT_MAX };

	if (!open) {
		from = RAST_ARRAY_LAST(vertices);
		to = &vertices[0];
	}

	while (to != &vertices[vertexCount]) {
		float accumulatedLength = 0;

		_RastPathAddVertex(&dash, from);

		while (to != &vertices[vertexCount]) {
			float dx = to->x - from.x, dy = to->y - from.y;
			float distance = EsCRTsqrtf(dx * dx + dy * dy);

			if (accumulatedLength + distance >= style->length) {
				float fraction = (style->length - accumulatedLength) / distance;
				RastVertex stop = { from.x + fraction * dx, from.y + fraction * dy };
				_RastPathAddVertex(&dash, stop);
				from = stop;
				break;
			}

			accumulatedLength += distance;
			from = *to;
			_RastPathAddVertex(&dash, from);
			to++;
		}

		_RastShapeCreateContour(&shape, &dash, *style->style, true);

		accumulatedLength = 0;

		while (to != &vertices[vertexCount]) {
			float dx = to->x - from.x, dy = to->y - from.y;
			float distance = EsCRTsqrtf(dx * dx + dy * dy);

			if (accumulatedLength + distance >= style->gap) {
				float fraction = (style->gap - accumulatedLength) / distance;
				RastVertex stop = { from.x + fraction * dx, from.y + fraction * dy };
				from = stop;
				break;
			}

			accumulatedLength += distance;
			from = *to;
			to++;
		}

		style++;

		if (style == dashStyles + dashStyleCount) {
			style = dashStyles + 0;
		}

		RAST_ARRAY_CLEAR(dash.vertices);
	}

	return shape;
}

RastVertex _RastVertexScale(RastVertex vertex, RastVertex scale) {
	return (RastVertex) { vertex.x * scale.x, vertex.y * scale.y };
}

void _RastFlattenBezierRecursive(RastPath *path, RastVertex v1, RastVertex v2, RastVertex v3, RastVertex v4, int level) {
	if (level > 8) return;
	
	RastVertex v12 = RAST_AVERAGE_VERTICES(v1, v2);
	RastVertex v23 = RAST_AVERAGE_VERTICES(v2, v3);
	RastVertex v34 = RAST_AVERAGE_VERTICES(v3, v4);
	
	RastVertex delta = { v4.x - v1.x, v4.y - v1.y };
	float d = AbsoluteFloat((v2.x - v4.x) * delta.y + (v4.y - v2.y) * delta.x) 
		+ AbsoluteFloat((v3.x - v4.x) * delta.y + (v4.y - v3.y) * delta.x);
	
	if (d * d < RAST_FLATTEN_TOLERANCE * (delta.x * delta.x + delta.y * delta.y)) {
		_RastPathAddVertex(path, v4);
		return;
	}
	
	RastVertex v123  = RAST_AVERAGE_VERTICES(v12,  v23);
	RastVertex v234  = RAST_AVERAGE_VERTICES(v23,  v34);
	RastVertex v1234 = RAST_AVERAGE_VERTICES(v123, v234);
	
	_RastFlattenBezierRecursive(path, v1, v12, v123, v1234, level + 1);
	_RastFlattenBezierRecursive(path, v1234, v234, v34, v4, level + 1);
}

void RastPathAppendBezier(RastPath *path, const RastVertex *vertices, size_t vertexCount, RastVertex scale) {
	if (vertexCount < 4) return;
	_RastPathAddVertex(path, _RastVertexScale(vertices[0], scale));
	
	for (uintptr_t i = 0; i < vertexCount - 3; i += 3) {
		// TODO Scale the control points such that the center of the curve is aligned?
		_RastFlattenBezierRecursive(path, _RastVertexScale(vertices[i + 0], scale), _RastVertexScale(vertices[i + 1], scale), 
				_RastVertexScale(vertices[i + 2], scale), _RastVertexScale(vertices[i + 3], scale), 0);
	}
}

void RastPathAppendLinear(RastPath *path, RastVertex *vertices, size_t vertexCount, RastVertex scale) {
	if (!vertexCount) return;

	for (uintptr_t i = 0; i < vertexCount; i++) {
		_RastPathAddVertex(path, _RastVertexScale(vertices[i], scale));
	}
}

void RastPathAppendArc(RastPath *path, RastVertex center, float radius, float startAngle, float endAngle) {
	float deltaAngle = EsCRTacosf(1 - 0.5f * RAST_FLATTEN_TOLERANCE * RAST_FLATTEN_TOLERANCE / radius / radius); // From cosine rule.
	size_t steps = EsCRTfabsf(endAngle - startAngle) / deltaAngle;

	for (uintptr_t i = 0; i <= steps; i++) {
		float angle = (endAngle - startAngle) / steps * i + startAngle;
		RastVertex vertex;
		vertex.x = center.x + radius * EsCRTcosf(angle);
		vertex.y = center.y + radius * EsCRTsinf(angle);
		_RastPathAddVertex(path, vertex);
	}
}

void RastPathTranslate(RastPath *path, float x, float y) {
	if (!x && !y) return;
		
	for (uintptr_t i = 0; i < RAST_ARRAY_LENGTH_U(path->vertices); i++) {
		path->vertices[i].x += x;
		path->vertices[i].y += y;
	}
}

void RastPathTransform(RastPath *path, float *matrix) {
	for (uintptr_t i = 0; i < RAST_ARRAY_LENGTH_U(path->vertices); i++) {
		float x = path->vertices[i].x, y = path->vertices[i].y;
		path->vertices[i].x = matrix[0] * x + matrix[1] * y + matrix[2];
		path->vertices[i].y = matrix[3] * x + matrix[4] * y + matrix[5];
	}
}

void RastPathDestroy(RastPath *path) {
	RAST_ARRAY_FREE(path->segments);
	RAST_ARRAY_FREE(path->vertices);
}

float _RastInterpolateWithGamma(float from, float to, float progress) {
	from = from * from;
	to = to * to;
	return EsCRTsqrtf(from + progress * (to - from));
}

float _RastInterpolateSimple(float from, float to, float progress) {
	return from + progress * (to - from);
}

void RastGradientInitialise(RastPaint *paint, RastGradientStop *stops, size_t stopCount, bool useGammaInterpolation) {
	if (!stopCount) {
		return;
	}

	paint->gradient.color = (uint32_t *) EsHeapAllocate(4 * RAST_GRADIENT_COLORS, false);
	paint->gradient.alpha = (float *) EsHeapAllocate(sizeof(float) * RAST_GRADIENT_COLORS, false);
	
	for (uintptr_t stop = 0; stop < stopCount - 1; stop++) {
		float fa = ((stops[stop + 0].color >> 24) & 0xFF) / 255.0f;
		float fb = ((stops[stop + 0].color >> 16) & 0xFF) / 255.0f;
		float fg = ((stops[stop + 0].color >>  8) & 0xFF) / 255.0f;
		float fr = ((stops[stop + 0].color >>  0) & 0xFF) / 255.0f;
		float ta = ((stops[stop + 1].color >> 24) & 0xFF) / 255.0f;
		float tb = ((stops[stop + 1].color >> 16) & 0xFF) / 255.0f;
		float tg = ((stops[stop + 1].color >>  8) & 0xFF) / 255.0f;
		float tr = ((stops[stop + 1].color >>  0) & 0xFF) / 255.0f;
		
		int fi = RAST_GRADIENT_COLORS * (stop == 0 ? 0 : stops[stop + 0].position);
		int ti = RAST_GRADIENT_COLORS * (stop == stopCount - 2 ? 1 : stops[stop + 1].position);
		if (fi < 0) fi = 0;
		if (ti > RAST_GRADIENT_COLORS) ti = RAST_GRADIENT_COLORS;
		
		for (int i = fi; i < ti; i++) {
			float p = (float) (i - fi) / (ti - fi);
			paint->gradient.alpha[i] = fa + (ta - fa) * p;

			if (useGammaInterpolation) {
				paint->gradient.color[i] = (uint32_t) (_RastInterpolateWithGamma(fr, tr, p) * 255.0f) <<  0
							 | (uint32_t) (_RastInterpolateWithGamma(fg, tg, p) * 255.0f) <<  8
							 | (uint32_t) (_RastInterpolateWithGamma(fb, tb, p) * 255.0f) << 16;
			} else {
				paint->gradient.color[i] = (uint32_t) (_RastInterpolateSimple(fr, tr, p) * 255.0f) <<  0
							 | (uint32_t) (_RastInterpolateSimple(fg, tg, p) * 255.0f) <<  8
							 | (uint32_t) (_RastInterpolateSimple(fb, tb, p) * 255.0f) << 16;
			}
		}
	}
}

void RastGradientDestroy(RastPaint *paint) {
	if (paint->type == RAST_PAINT_LINEAR_GRADIENT 
			|| paint->type == RAST_PAINT_RADIAL_GRADIENT 
			|| paint->type == RAST_PAINT_ANGULAR_GRADIENT) {
		EsHeapFree(paint->gradient.color);
		EsHeapFree(paint->gradient.alpha);
	}
}

#ifndef IN_DESIGNER
void EsDrawLine(EsPainter *painter, float *vertices, size_t vertexCount, uint32_t color, float width, uint32_t flags) {
	RastSurface surface = {};
	surface.buffer = (uint32_t *) painter->target->bits;
	surface.stride = painter->target->stride;

	if (RastSurfaceInitialise(&surface, painter->target->width, painter->target->height, true)) {
		RastPath path = {};
		RastPathAppendLinear(&path, (RastVertex *) vertices, vertexCount, { 1, 1 });
		RastContourStyle style = {};
		style.externalWidth = width / 2.0f;
		style.internalWidth = width / 2.0f;
		style.capMode = (flags & ES_DRAW_LINE_CAP_ROUND) ? RAST_LINE_CAP_ROUND 
			: (flags & ES_DRAW_LINE_CAP_SQUARE) ? RAST_LINE_CAP_SQUARE 
			: RAST_LINE_CAP_FLAT;
		RastShape shape = RastShapeCreateContour(&path, style, true);
		RastPaint paint = {};
		paint.type = RAST_PAINT_SOLID;
		paint.solid.color = color & 0xFFFFFF;
		paint.solid.alpha = (color >> 24) / 255.0f;
		RastSurfaceFill(surface, shape, paint, false);
		RastPathDestroy(&path);
	}
	
	RastSurfaceDestroy(&surface);
}
#endif
