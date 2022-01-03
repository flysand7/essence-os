// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

/////////////////////////////////
// Basic utilities.
/////////////////////////////////

#if defined(SHARED_MATH_WANT_BASIC_UTILITIES) || defined(SHARED_MATH_WANT_ALL)

template <class T>
inline T RoundDown(T value, T divisor) {
	value /= divisor;
	value *= divisor;
	return value;
}

template <class T>
inline T RoundUp(T value, T divisor) {
	value += divisor - 1;
	value /= divisor;
	value *= divisor;
	return value;
}

inline int DistanceSquared(int x, int y) {
	return x * x + y * y;
}

inline int DistanceSquared(int x1, int y1, int x2, int y2) {
	int dx = x2 - x1;
	int dy = y2 - y1;
	return dx * dx + dy * dy;
}

__attribute__((no_instrument_function))
inline int ClampInteger(int low, int high, int integer) {
	if (integer < low) return low;
	if (integer > high) return high;
	return integer;
}

inline double ClampDouble(double low, double high, double x) {
	if (x < low) return low;
	if (x > high) return high;
	return x;
}

inline intptr_t ClampIntptr(intptr_t low, intptr_t high, intptr_t integer) {
	if (integer < low) return low;
	if (integer > high) return high;
	return integer;
}

__attribute__((no_instrument_function))
inline int MaximumInteger(int a, int b) {
	return a > b ? a : b;
}

#define MaximumInteger3 MaximumInteger
#define MinimumInteger3 MinimumInteger

__attribute__((no_instrument_function))
inline int MaximumInteger(int a, int b, int c) {
	return MaximumInteger(MaximumInteger(a, b), c);
}

__attribute__((no_instrument_function))
inline int MaximumInteger(int a, int b, int c, int d) {
	return MaximumInteger(MaximumInteger(a, b, c), d);
}

__attribute__((no_instrument_function))
inline int MinimumInteger(int a, int b) {
	return a < b ? a : b;
}

inline float AbsoluteFloat(float f) {
	return f > 0 ? f : -f;
}

inline float SignFloat(float f) {
	return f < 0 ? -1 : f > 0 ? 1 : 0;
}

inline int AbsoluteInteger(int a) {
	return a > 0 ? a : -a;
}

inline int64_t AbsoluteInteger64(int64_t a) {
	return a > 0 ? a : -a;
}

#endif

/////////////////////////////////
// Interpolation.
/////////////////////////////////

#if defined(SHARED_MATH_WANT_INTERPOLATION) || defined(SHARED_MATH_WANT_ALL)

float LinearMap(float inFrom, float inTo, float outFrom, float outTo, float value) {
	float raw = (value - inFrom) / (inTo - inFrom);
	return raw * (outTo - outFrom) + outFrom;
}

__attribute__((no_instrument_function))
float LinearInterpolate(float from, float to, float progress) {
	return from + progress * (to - from);
}

float RubberBand(float original, float target) {
	float sign = SignFloat(original - target);
	float distance = AbsoluteFloat(original - target);
	float amount = EsCRTlog2f(distance);
	return target + sign * amount * 2.0f;
}

float GammaInterpolate(float from, float to, float progress) {
	from = from * from;
	to = to * to;
	return EsCRTsqrtf(from + progress * (to - from));
}

double SmoothAnimationTime(double progress) {
	if (progress > 1) return 1;
	progress -= 1;
	return 1 + progress * progress * progress;
}

double SmoothAnimationTimeSharp(double progress) {
	if (progress > 1) return 1;
	progress -= 1;
	double progressSquared = progress * progress;
	return 1 + progressSquared * progressSquared * progress;
}

#ifndef EsColorInterpolate
uint32_t EsColorInterpolate(uint32_t from, uint32_t to, float progress) {
	float fa = ((from >> 24) & 0xFF) / 255.0f;
	float fb = ((from >> 16) & 0xFF) / 255.0f;
	float fg = ((from >>  8) & 0xFF) / 255.0f;
	float fr = ((from >>  0) & 0xFF) / 255.0f;
	float ta = ((to   >> 24) & 0xFF) / 255.0f;
	float tb = ((to   >> 16) & 0xFF) / 255.0f;
	float tg = ((to   >>  8) & 0xFF) / 255.0f;
	float tr = ((to   >>  0) & 0xFF) / 255.0f;

	if (fa && !ta) { tr = fr, tg = fg, tb = fb; }
	if (ta && !fa) { fr = tr, fg = tg, fb = tb; }

	return (uint32_t) (LinearInterpolate(fr, tr, progress) * 255.0f) << 0
		| (uint32_t) (LinearInterpolate(fg, tg, progress) * 255.0f) << 8
		| (uint32_t) (LinearInterpolate(fb, tb, progress) * 255.0f) << 16
		| (uint32_t) (LinearInterpolate(fa, ta, progress) * 255.0f) << 24;
}
#endif

#ifndef EsRectangleLinearInterpolate
EsRectangle EsRectangleLinearInterpolate(EsRectangle from, EsRectangle to, float progress) {
	return ES_RECT_4(LinearInterpolate(from.l, to.l, progress), LinearInterpolate(from.r, to.r, progress), 
			LinearInterpolate(from.t, to.t, progress), LinearInterpolate(from.b, to.b, progress));
}
#endif

#endif

/////////////////////////////////
// HSV colors.
/////////////////////////////////

#ifdef SHARED_MATH_WANT_ALL

uint32_t EsColorConvertToRGB(float h, float s, float v) {
	float r = 0, g = 0, b = 0;

	if (!s) {
		r = g = b = v;
	} else {
		float f = h - EsCRTfloorf(h);
		float x = v * (1 - s), y = v * (1 - s * f), z = v * (1 - s * (1 - f));

		switch ((int) h) {
			case 0: r = v, g = z, b = x; break;
			case 1: r = y, g = v, b = x; break;
			case 2: r = x, g = v, b = z; break;
			case 3: r = x, g = y, b = v; break;
			case 4: r = z, g = x, b = v; break;
			case 5: r = v, g = x, b = y; break;
		}
	}

	return (((uint32_t) (r * 255)) << 16) | (((uint32_t) (g * 255)) << 8) | (((uint32_t) (b * 255)) << 0);
}

bool EsColorConvertToHSV(uint32_t color, float *h, float *s, float *v) {
	float r = (float) ((color >> 16) & 0xFF) / 255.0f;
	float g = (float) ((color >>  8) & 0xFF) / 255.0f;
	float b = (float) ((color >>  0) & 0xFF) / 255.0f;

	float maximum = (r > g && r > b) ? r : (g > b ? g : b),
	      minimum = (r < g && r < b) ? r : (g < b ? g : b),
	      difference = maximum - minimum;
	*v = maximum;

	if (!difference) {
		*s = 0;
		return false;
	} else {
		if (r == maximum) *h = (g - b) / difference + 0;
		if (g == maximum) *h = (b - r) / difference + 2;
		if (b == maximum) *h = (r - g) / difference + 4;
		if (*h < 0) *h += 6;
		*s = difference / maximum;
		return true;
	}
}

bool EsColorIsLight(uint32_t color) {
	float r = (color & 0xFF0000) >> 16;
	float g = (color & 0x00FF00) >>  8;
	float b = (color & 0x0000FF) >>  0;
	float brightness = r * r * 0.241f + g * g * 0.691f + b * b * 0.068f;
	return brightness >= 180.0f * 180.0f;
}

#endif

/////////////////////////////////
// Standard mathematical functions.
/////////////////////////////////

#ifdef SHARED_MATH_WANT_ALL

union ConvertFloatInteger {
	float f;
	uint32_t i;
};

union ConvertDoubleInteger {
	double d;
	uint64_t i;
};

float EsCRTfloorf(float x) {
	ConvertFloatInteger convert = {x};
	uint32_t sign = convert.i & 0x80000000;
	int exponent = (int) ((convert.i >> 23) & 0xFF) - 0x7F;

	if (exponent >= 23) {
		// There aren't any bits representing a fractional part.
	} else if (exponent >= 0) {
		// Positive exponent.
		uint32_t mask = 0x7FFFFF >> exponent;
		if (!(mask & convert.i)) return x; // Already an integer.
		if (sign) convert.i += mask;
		convert.i &= ~mask; // Mask out the fractional bits.
	} else if (exponent < 0) {
		// Negative exponent.
		return sign ? -1.0 : 0.0;
	}

	return convert.f;
}

double EsCRTfloor(double x) {
	if (x == 0) return x;

	const double doubleToInteger = 1.0 / 2.22044604925031308085e-16;

	ConvertDoubleInteger convert = {x};
	uint64_t sign = convert.i & 0x8000000000000000;
	int exponent = (int) ((convert.i >> 52) & 0x7FF) - 0x3FF;

	if (exponent >= 52) {
		// There aren't any bits representing a fractional part.
		return x;
	} else if (exponent >= 0) {
		// Positive exponent.
		double y = sign ? (x - doubleToInteger + doubleToInteger - x) : (x + doubleToInteger - doubleToInteger - x);
		return y > 0 ? x + y - 1 : x + y;
	} else if (exponent < 0) {
		// Negative exponent.
		return sign ? -1.0 : 0.0;
	}

	return 0;
}

double EsCRTceil(double x) {
	if (x == 0) return x;

	const double doubleToInteger = 1.0 / 2.22044604925031308085e-16;

	ConvertDoubleInteger convert = {x};
	uint64_t sign = convert.i & 0x8000000000000000;
	int exponent = (int) ((convert.i >> 52) & 0x7FF) - 0x3FF;

	if (exponent >= 52) {
		// There aren't any bits representing a fractional part.
		return x;
	} else if (exponent >= 0) {
		// Positive exponent.
		double y = sign ? (x - doubleToInteger + doubleToInteger - x) : (x + doubleToInteger - doubleToInteger - x);
		return y < 0 ? x + y + 1 : x + y;
	} else if (exponent < 0) {
		// Negative exponent.
		return sign ? -0.0 : 1.0;
	}

	return 0;
}

double EsCRTfabs(double x) {
	ConvertDoubleInteger convert = {x};
	convert.i &= ~0x8000000000000000;
	return convert.d;
}

float EsCRTfabsf(float x) {
	ConvertFloatInteger convert = {x};
	convert.i &= ~0x80000000;
	return convert.f;
}

float EsCRTceilf(float x) {
	if (x == 0) return x;

	ConvertFloatInteger convert = {x};
	uint32_t sign = convert.i & 0x80000000;
	int exponent = (int) ((convert.i >> 23) & 0xFF) - 0x7F;

	if (exponent >= 23) {
		// There aren't any bits representing a fractional part.
	} else if (exponent >= 0) {
		// Positive exponent.
		uint32_t mask = 0x7FFFFF >> exponent;
		if (!(mask & convert.i)) return x; // Already an integer.
		if (!sign) convert.i += mask;
		convert.i &= ~mask; // Mask out the fractional bits.
	} else if (exponent < 0) {
		// Negative exponent.
		return sign ? -0.0 : 1.0;
	}

	return convert.f;
}

#define D(x) (((ConvertDoubleInteger) { .i = (x) }).d)
#define F(x) (((ConvertFloatInteger) { .i = (x) }).f)

double _Sine(double x) {
	// Calculates sin(x) for x in [0, pi/4].

	double x2 = x * x;

	return x * (D(0x3FF0000000000000) + x2 * (D(0xBFC5555555555540) + x2 * (D(0x3F8111111110ED80) + x2 * (D(0xBF2A01A019AE6000) 
			+ x2 * (D(0x3EC71DE349280000) + x2 * (D(0xBE5AE5DC48000000) + x2 * D(0x3DE5D68200000000)))))));
}

float _SineFloat(float x) {
	// Calculates sin(x) for x in [0, pi/4].

	float x2 = x * x;

	return x * (F(0x3F800000) + x2 * (F(0xBE2AAAA0) + x2 * (F(0x3C0882C0) + x2 * F(0xB94C6000))));
}

double _ArcSine(double x) {
	// Calculates arcsin(x) for x in [0, 0.5].

	double x2 = x * x;

	return x * (D(0x3FEFFFFFFFFFFFE6) + x2 * (D(0x3FC555555555FE00) + x2 * (D(0x3FB333333292DF90) + x2 * (D(0x3FA6DB6DFD3693A0) 
			+ x2 * (D(0x3F9F1C608DE51900) + x2 * (D(0x3F96EA0659B9A080) + x2 * (D(0x3F91B4ABF1029100) 
			+ x2 * (D(0x3F8DA8DAF31ECD00) + x2 * (D(0x3F81C01FD5000C00) + x2 * (D(0x3F94BDA038CF6B00)
			+ x2 * (D(0xBF8E849CA75B1E00) + x2 * D(0x3FA146C2D37F2C60))))))))))));
}

float _ArcSineFloat(float x) {
	// Calculates arcsin(x) for x in [0, 0.5].

	float x2 = x * x;

	return x * (F(0x3F800004) + x2 * (F(0x3E2AA130) + x2 * (F(0x3D9B2C28) + x2 * (F(0x3D1C1800) + x2 * F(0x3D5A97C0)))));
}

double _ArcTangent(double x) {
	// Calculates arctan(x) for x in [0, 0.5].

	double x2 = x * x;

	return x * (D(0x3FEFFFFFFFFFFFF8) + x2 * (D(0xBFD5555555553B44) + x2 * (D(0x3FC9999999803988) + x2 * (D(0xBFC249248C882E80) 
			+ x2 * (D(0x3FBC71C5A4E4C220) + x2 * (D(0xBFB745B3B75243F0) + x2 * (D(0x3FB3AFAE9A2939E0) 
			+ x2 * (D(0xBFB1030C4A4A1B90) + x2 * (D(0x3FAD6F65C35579A0) + x2 * (D(0xBFA805BCFDAFEDC0)
			+ x2 * (D(0x3F9FC6B5E115F2C0) + x2 * D(0xBF87DCA5AB25BF80))))))))))));
}

float _ArcTangentFloat(float x) {
	// Calculates arctan(x) for x in [0, 0.5].

	float x2 = x * x;

	return x * (F(0x3F7FFFF8) + x2 * (F(0xBEAAA53C) + x2 * (F(0x3E4BC990) + x2 * (F(0xBE084A60) + x2 * F(0x3D8864B0)))));
}

double _Cosine(double x) {
	// Calculates cos(x) for x in [0, pi/4].

	double x2 = x * x;

	return D(0x3FF0000000000000) + x2 * (D(0xBFDFFFFFFFFFFFA0) + x2 * (D(0x3FA555555554F7C0) + x2 * (D(0xBF56C16C16475C00) 
			+ x2 * (D(0x3EFA019F87490000) + x2 * (D(0xBE927DF66B000000) + x2 * D(0x3E21B949E0000000))))));
}

float _CosineFloat(float x) {
	// Calculates cos(x) for x in [0, pi/4].

	float x2 = x * x;

	return F(0x3F800000) + x2 * (F(0xBEFFFFDA) + x2 * (F(0x3D2A9F60) + x2 * F(0xBAB22C00)));
}

double _Tangent(double x) {
	// Calculates tan(x) for x in [0, pi/4].

	double x2 = x * x;

	return x * (D(0x3FEFFFFFFFFFFFE8) + x2 * (D(0x3FD5555555558000) + x2 * (D(0x3FC1111110FACF90) + x2 * (D(0x3FABA1BA266BFD20) 
			+ x2 * (D(0x3F9664F30E56E580) + x2 * (D(0x3F822703B08BDC00) + x2 * (D(0x3F6D698D2E4A4C00) 
			+ x2 * (D(0x3F57FF4F23EA4400) + x2 * (D(0x3F424F3BEC845800) + x2 * (D(0x3F34C78CA9F61000)
			+ x2 * (D(0xBF042089F8510000) + x2 * (D(0x3F29D7372D3A8000) + x2 * (D(0xBF19D1C5EF6F0000)
			+ x2 * (D(0x3F0980BDF11E8000)))))))))))))));
}

float _TangentFloat(float x) {
	// Calculates tan(x) for x in [0, pi/4].

	float x2 = x * x;

	return x * (F(0x3F800001) + x2 * (F(0x3EAAA9AA) + x2 * (F(0x3E08ABA8) + x2 * (F(0x3D58EC90) 
			+ x2 * (F(0x3CD24840) + x2 * (F(0x3AC3CA00) + x2 * F(0x3C272F00)))))));
}

double EsCRTsin(double x) {
	bool negate = false;

	// x in -infty, infty

	if (x < 0) {
		x = -x;
		negate = true;
	}

	// x in 0, infty

	x -= 2 * ES_PI * EsCRTfloor(x / (2 * ES_PI));

	// x in 0, 2*pi

	if (x < ES_PI / 2) {
	} else if (x < ES_PI) {
		x = ES_PI - x;
	} else if (x < 3 * ES_PI / 2) {
		x = x - ES_PI;
		negate = !negate;
	} else {
		x = ES_PI * 2 - x;
		negate = !negate;
	}

	// x in 0, pi/2

	double y = x < ES_PI / 4 ? _Sine(x) : _Cosine(ES_PI / 2 - x);
	return negate ? -y : y;
}

float EsCRTsinf(float x) {
	bool negate = false;

	// x in -infty, infty

	if (x < 0) {
		x = -x;
		negate = true;
	}

	// x in 0, infty

	x -= 2 * ES_PI * EsCRTfloorf(x / (2 * ES_PI));

	// x in 0, 2*pi

	if (x < ES_PI / 2) {
	} else if (x < ES_PI) {
		x = ES_PI - x;
	} else if (x < 3 * ES_PI / 2) {
		x = x - ES_PI;
		negate = !negate;
	} else {
		x = ES_PI * 2 - x;
		negate = !negate;
	}

	// x in 0, pi/2

	float y = x < ES_PI / 4 ? _SineFloat(x) : _CosineFloat(ES_PI / 2 - x);
	return negate ? -y : y;
}

double EsCRTcos(double x) {
	bool negate = false;

	// x in -infty, infty

	if (x < 0) {
		x = -x;
	}

	// x in 0, infty

	x -= 2 * ES_PI * EsCRTfloor(x / (2 * ES_PI));

	// x in 0, 2*pi

	if (x < ES_PI / 2) {
	} else if (x < ES_PI) {
		x = ES_PI - x;
		negate = !negate;
	} else if (x < 3 * ES_PI / 2) {
		x = x - ES_PI;
		negate = !negate;
	} else {
		x = ES_PI * 2 - x;
	}

	// x in 0, pi/2

	double y = x < ES_PI / 4 ? _Cosine(x) : _Sine(ES_PI / 2 - x);
	return negate ? -y : y;
}

float EsCRTcosf(float x) {
	bool negate = false;

	// x in -infty, infty

	if (x < 0) {
		x = -x;
	}

	// x in 0, infty

	x -= 2 * ES_PI * EsCRTfloorf(x / (2 * ES_PI));

	// x in 0, 2*pi

	if (x < ES_PI / 2) {
	} else if (x < ES_PI) {
		x = ES_PI - x;
		negate = !negate;
	} else if (x < 3 * ES_PI / 2) {
		x = x - ES_PI;
		negate = !negate;
	} else {
		x = ES_PI * 2 - x;
	}

	// x in 0, pi/2

	float y = x < ES_PI / 4 ? _CosineFloat(x) : _SineFloat(ES_PI / 2 - x);
	return negate ? -y : y;
}

double EsCRTtan(double x) {
	bool negate = false;

	// x in -infty, infty

	if (x < 0) {
		x = -x;
		negate = !negate;
	}

	// x in 0, infty

	x -= ES_PI * EsCRTfloor(x / ES_PI);

	// x in 0, pi

	if (x > ES_PI / 2) {
		x = ES_PI - x;
		negate = !negate;
	}

	// x in 0, pi/2

	double y = x < ES_PI / 4 ? _Tangent(x) : (1.0 / _Tangent(ES_PI / 2 - x));
	return negate ? -y : y;
}

float EsCRTtanf(float x) {
	bool negate = false;

	// x in -infty, infty

	if (x < 0) {
		x = -x;
		negate = !negate;
	}

	// x in 0, infty

	x -= ES_PI * EsCRTfloorf(x / ES_PI);

	// x in 0, pi

	if (x > ES_PI / 2) {
		x = ES_PI - x;
		negate = !negate;
	}

	// x in 0, pi/2

	float y = x < ES_PI / 4 ? _TangentFloat(x) : (1.0 / _TangentFloat(ES_PI / 2 - x));
	return negate ? -y : y;
}

double EsCRTasin(double x) {
	bool negate = false;

	if (x < 0) { 
		x = -x; 
		negate = true; 
	}

	double y;

	if (x < 0.5) {
		y = _ArcSine(x);
	} else {
		y = ES_PI / 2 - 2 * _ArcSine(EsCRTsqrt(0.5 - 0.5 * x));
	}
	
	return negate ? -y : y;
}

float EsCRTasinf(float x) {
	bool negate = false;

	if (x < 0) { 
		x = -x; 
		negate = true; 
	}

	float y;

	if (x < 0.5) {
		y = _ArcSineFloat(x);
	} else {
		y = ES_PI / 2 - 2 * _ArcSineFloat(EsCRTsqrtf(0.5 - 0.5 * x));
	}
	
	return negate ? -y : y;
}

double EsCRTacos(double x) {
	return EsCRTasin(-x) + ES_PI / 2;
}

float EsCRTacosf(float x) {
	return EsCRTasinf(-x) + ES_PI / 2;
}

double EsCRTatan(double x) {
	bool negate = false;

	if (x < 0) { 
		x = -x; 
		negate = true; 
	}

	bool reciprocalTaken = false;

	if (x > 1) {
		x = 1 / x;
		reciprocalTaken = true;
	}

	double y;

	if (x < 0.5) {
		y = _ArcTangent(x);
	} else {
		y = 0.463647609000806116 + _ArcTangent((2 * x - 1) / (2 + x));
	}

	if (reciprocalTaken) {
		y = ES_PI / 2 - y;
	}
	
	return negate ? -y : y;
}

float EsCRTatanf(float x) {
	bool negate = false;

	if (x < 0) { 
		x = -x; 
		negate = true; 
	}

	bool reciprocalTaken = false;

	if (x > 1) {
		x = 1 / x;
		reciprocalTaken = true;
	}

	float y;

	if (x < 0.5f) {
		y = _ArcTangentFloat(x);
	} else {
		y = 0.463647609000806116f + _ArcTangentFloat((2 * x - 1) / (2 + x));
	}

	if (reciprocalTaken) {
		y = ES_PI / 2 - y;
	}
	
	return negate ? -y : y;
}

double EsCRTatan2(double y, double x) {
	if (x == 0) return y > 0 ? ES_PI / 2 : -ES_PI / 2;
	else if (x > 0) return EsCRTatan(y / x);
	else if (y >= 0) return ES_PI + EsCRTatan(y / x);
	else return -ES_PI + EsCRTatan(y / x);
}

float EsCRTatan2f(float y, float x) {
	if (x == 0) return y > 0 ? ES_PI / 2 : -ES_PI / 2;
	else if (x > 0) return EsCRTatanf(y / x);
	else if (y >= 0) return ES_PI + EsCRTatanf(y / x);
	else return -ES_PI + EsCRTatanf(y / x);
}

double EsCRTexp2(double x) {
	double a = EsCRTfloor(x * 8);
	int64_t ai = a;

	if (ai < -1024) {
		return 0;
	}

	double b = x - a / 8;

	double y = D(0x3FF0000000000000) + b * (D(0x3FE62E42FEFA3A00) + b * (D(0x3FCEBFBDFF829140) 
			+ b * (D(0x3FAC6B08D73C4A40) + b * (D(0x3F83B2AB53873280) + b * (D(0x3F55D88F363C6C00) 
			+ b * (D(0x3F242C003E4A2000) + b * D(0x3EF0B291F6C00000)))))));

	const double m[8] = {
		D(0x3FF0000000000000),
		D(0x3FF172B83C7D517B),
		D(0x3FF306FE0A31B715),
		D(0x3FF4BFDAD5362A27),
		D(0x3FF6A09E667F3BCD),
		D(0x3FF8ACE5422AA0DB),
		D(0x3FFAE89F995AD3AD),
		D(0x3FFD5818DCFBA487),
	};

	y *= m[ai & 7];

	ConvertDoubleInteger c;
	c.d = y;
	c.i += (ai >> 3) << 52;
	return c.d;
}

float EsCRTexp2f(float x) {
	float a = EsCRTfloorf(x);
	int32_t ai = a;

	if (ai < -128) {
		return 0;
	}

	float b = x - a;

	float y = F(0x3F7FFFFE) + b * (F(0x3F31729A) + b * (F(0x3E75E700) 
			+ b * (F(0x3D64D520) + b * (F(0x3C128280) + b * F(0x3AF89400)))));

	ConvertFloatInteger c;
	c.f = y;
	c.i += ai << 23;
	return c.f;
}

double EsCRTlog2(double x) {
	ConvertDoubleInteger c;
	c.d = x;
	int64_t e = ((c.i >> 52) & 2047) - 0x3FF;
	c.i = (c.i & ~((uint64_t) 0x7FF << 52)) + ((uint64_t) 0x3FF << 52);
	x = c.d;

	double a;

	if (x < 1.125) {
		a = 0;
	} else if (x < 1.250) {
		x *= 1.125 / 1.250;
		a = D(0xBFC374D65D9E608E);
	} else if (x < 1.375) {
		x *= 1.125 / 1.375;
		a = D(0xBFD28746C334FECB);
	} else if (x < 1.500) {
		x *= 1.125 / 1.500;
		a = D(0xBFDA8FF971810A5E);
	} else if (x < 1.625) {
		x *= 1.125 / 1.625;
		a = D(0xBFE0F9F9FFC8932A);
	} else if (x < 1.750) {
		x *= 1.125 / 1.750;
		a = D(0xBFE465D36ED11B11);
	} else if (x < 1.875) {
		x *= 1.125 / 1.875;
		a = D(0xBFE79538DEA712F5);
	} else {
		x *= 1.125 / 2.000;
		a = D(0xBFEA8FF971810A5E);
	}

	double y = D(0xC00FF8445026AD97) + x * (D(0x40287A7A02D9353F) + x * (D(0xC03711C58D55CEE2) 
			+ x * (D(0x4040E8263C321A26) + x * (D(0xC041EB22EA691BB3) + x * (D(0x403B00FB376D1F10) 
			+ x * (D(0xC02C416ABE857241) + x * (D(0x40138BA7FAA3523A) + x * (D(0xBFF019731AF80316) 
			+ x * D(0x3FB7F1CD3852C200)))))))));

	return y - a + e;
}

float EsCRTlog2f(float x) {
	ConvertFloatInteger c;
	c.f = x;
	int32_t e = ((c.i >> 23) & 255) - 0x7F;
	c.i = (c.i & ~(0xFF << 23)) + (0x7F << 23);
	x = c.f;

	double y = F(0xC05B5154) + x * (F(0x410297C6) + x * (F(0xC1205CEB) 
			+ x * (F(0x4114DF63) + x * (F(0xC0C0DBBB) + x * (F(0x402942C6) 
			+ x * (F(0xBF3FF98A) + x * (F(0x3DFE1050) + x * F(0xBC151480))))))));

	return y + e;
}

double EsCRTpow(double x, double y) {
	return EsCRTexp2(y * EsCRTlog2(x));
}

float EsCRTpowf(float x, float y) {
	return EsCRTexp2f(y * EsCRTlog2f(x));
}

double EsCRTcbrt(double x) {
	if (x >= 0.0) return EsCRTpow(x, 1.0f / 3.0);
	else return -EsCRTpow(-x, 1.0f / 3.0);
}

float EsCRTcbrtf(float x) {
	if (x >= 0.0f) return EsCRTpowf(x, 1.0f / 3.0f);
	else return -EsCRTpowf(-x, 1.0f / 3.0f);
}

double EsCRTexp(double x) {
	return EsCRTexp2(x * 1.4426950408889634073);
}

float EsCRTexpf(float x) {
	return EsCRTexp2f(x * 1.442695040f);
}

double EsCRTfmod(double x, double y) {
	double n = x / y;
	return x - y * (n > 0.0 ? EsCRTfloor(n) : EsCRTceil(n));
}

float EsCRTfmodf(float x, float y) {
	float n = x / y;
	return x - y * (n > 0.0f ? EsCRTfloorf(n) : EsCRTceilf(n));
}

bool EsCRTisnanf(float x) {
	ConvertFloatInteger c;
	c.f = x;
	return (c.i & ~(1 << 31)) > 0x7F800000;
}

#undef D
#undef F

#endif

/////////////////////////////////
// High precision floats.
/////////////////////////////////

#ifdef SHARED_MATH_WANT_ALL

#define MANTISSA_BITS (256)
#include <x86intrin.h>

struct BigFloat {
	uint64_t mantissa[MANTISSA_BITS / 64];
	int32_t exponent;
	bool negative, zero;

#define SET_MANTISSA_BIT(m, i, value) \
	do { \
		if ((value)) { \
			(m).mantissa[(i) >> 6] |= (uint64_t) 1 << (63 - ((i) & 63)); \
		} else { \
			(m).mantissa[(i) >> 6] &= ~((uint64_t) 1 << (63 - ((i) & 63))); \
		} \
	} while (0)

#define GET_MANTISSA_BIT(m, i) (((m).mantissa[(i) >> 6] & ((uint64_t) 1 << (63 - ((i) & 63)))) ? 1 : 0)

	void _FromDouble(double d) {
		if (d == 0) {
			zero = true;
			return;
		}

		ConvertDoubleInteger c;
		c.d = d;

		negative = false;

		if (c.i & ((uint64_t) 1 << 63)) {
			negative = true;
		}

		exponent = ((c.i >> 52) & 2047) - 1023;

		for (uintptr_t i = 0; i < sizeof(mantissa) / sizeof(mantissa[0]); i++) {
			mantissa[i] = 0;
		}

		SET_MANTISSA_BIT(*this, 0, 1);

		for (intptr_t i = 0; i < 52; i++) {
			if (c.i & ((uint64_t) 1 << (51 - i))) {
				SET_MANTISSA_BIT(*this, i + 1, 1);
			}
		}

		_Normalize();
	}

	double ToDouble() {
		if (zero) return 0;

		double d = 0;

		for (intptr_t i = MANTISSA_BITS - 1; i >= 0; i--) {
			if (GET_MANTISSA_BIT(*this, i)) {
				ConvertDoubleInteger c;
				uint64_t p = exponent - i - 1 + 1024;
				c.i = p << 52;
				d += c.d;
			}
		}

		return negative ? -d : d;
	}

	void _ShiftRight(intptr_t shift) {
		if (!shift) return;
		
#if 0
		for (intptr_t i = MANTISSA_BITS - 1; i >= 0; i--) {
			if (i < shift) {
				SET_MANTISSA_BIT(*this, i, 0);
			} else {
				SET_MANTISSA_BIT(*this, i, GET_MANTISSA_BIT(*this, i - shift));
			}
		}
#else
		if (shift >= 64) {
			intptr_t shift0 = shift / 64;
			
			for (intptr_t i = sizeof(mantissa) / sizeof(mantissa[0]) - 1; i >= 0; i--) {
				if (i < shift0) {
					mantissa[i] = 0;
				} else {
					mantissa[i] = mantissa[i - shift0];
				}
			}
		}
		
		uint64_t removed = 0;
		
		for (intptr_t i = 0; i < (intptr_t) (sizeof(mantissa) / sizeof(mantissa[0])); i++) {
			uint64_t r = mantissa[i] & (((uint64_t) 1 << shift) - 1);
			mantissa[i] = (mantissa[i] >> shift) | removed;
			removed = r << (64 - shift);
		}
#endif
		
		exponent += shift;
	}

	void _ShiftLeft(intptr_t shift) {
		if (!shift) return;
		
#if 0
		for (intptr_t i = 0; i < MANTISSA_BITS; i++) {
			if (i >= MANTISSA_BITS - shift) {
				SET_MANTISSA_BIT(*this, i, 0);
			} else {
				SET_MANTISSA_BIT(*this, i, GET_MANTISSA_BIT(*this, i + shift));
			}
		}
#else
		if (shift >= 64) {
			intptr_t shift0 = shift / 64;
			
			for (intptr_t i = 0; i < (intptr_t) (sizeof(mantissa) / sizeof(mantissa[0])); i++) {
				if (i >= (intptr_t) (sizeof(mantissa) / sizeof(mantissa[0])) - shift0) {
					mantissa[i] = 0;
				} else {
					mantissa[i] = mantissa[i + shift0];
				}
			}
		}
		
		uint64_t removed = 0;
		
		for (intptr_t i = sizeof(mantissa) / sizeof(mantissa[0]) - 1; i >= 0; i--) {
			uint64_t r = mantissa[i] & (((1UL << shift) - 1) << (64 - shift));
			mantissa[i] = (mantissa[i] << shift) | removed;
			removed = r >> (64 - shift);
		}
#endif
		
		exponent -= shift;
	}

	void _Normalize() {
		// Find the first set bit.

		intptr_t shift = -1;

#if 0
		for (intptr_t i = 0; i < MANTISSA_BITS; i++) {
			if (GET_MANTISSA_BIT(*this, i)) {
				shift = i;
				break;
			}
		}
#else
		for (uintptr_t i = 0; i < sizeof(mantissa) / sizeof(mantissa[0]); i++) {
			if (mantissa[i]) {
				shift = __builtin_clzll(mantissa[i]) + i * 64;
				break;
			}
		}
#endif

		// If we didn't find one, then the number is zero.

		if (shift == -1) {
			exponent = 0;
			negative = false;
			zero = true;
			return;
		}

		// Shift the mantissa so that the first set bit is the first bit.

		if (shift) {
			_ShiftLeft(shift);
		}

		zero = false;
	}

	BigFloat operator+(BigFloat y) {
		if (zero) return y;
		if (y.zero) return *this;

		BigFloat x = *this;

		// Make sure that x has a bigger exponent.
		
		if (y.exponent > x.exponent) {
			BigFloat swap = x;
			x = y;
			y = swap;
		}

		// Shift y's mantissa to the right so that its exponent matches x's.

		y._ShiftRight(x.exponent - y.exponent);

		// If x and y have different signs, make sure y is the negative one.

		bool subtract = x.negative != y.negative;

		if (x.negative && !y.negative) {
			BigFloat swap = x;
			x = y;
			y = swap;
		}

		// Add/subtract y's mantissa to x's.

		if (subtract) {
			for (uintptr_t i = 0; i < sizeof(mantissa) / sizeof(mantissa[0]); i++) {
				y.mantissa[i] = ~y.mantissa[i];
			}
		}
		
		uint8_t carry = subtract ? 1 : 0;

#ifdef ES_BITS_32
		for (intptr_t i = MANTISSA_BITS - 1; i >= 0; i--) {
			uint8_t xi = GET_MANTISSA_BIT(x, i);
			uint8_t yi = GET_MANTISSA_BIT(y, i);
			uint8_t sum = xi + yi + carry;
			carry = sum >> 1;
			SET_MANTISSA_BIT(x, i, sum & 1);
		}
#else
		for (intptr_t i = sizeof(mantissa) / sizeof(mantissa[0]) - 1; i >= 0; i--) {
			carry = _addcarry_u64(carry, x.mantissa[i], y.mantissa[i], (long long unsigned int *) &x.mantissa[i]);
		}
#endif

		if (subtract) {
			if (!carry) {
				// Negate the result.

				for (uintptr_t i = 0; i < sizeof(mantissa) / sizeof(mantissa[0]); i++) {
					x.mantissa[i] = ~x.mantissa[i];
				}

				uint8_t carry = 1;
				
#ifdef ES_BITS_32
				for (intptr_t i = MANTISSA_BITS - 1; i >= 0; i--) {
					uint8_t xi = GET_MANTISSA_BIT(x, i);
					uint8_t sum = xi + carry;
					carry = sum >> 1;
					SET_MANTISSA_BIT(x, i, sum & 1);
				}
#else
				for (intptr_t i = sizeof(mantissa) / sizeof(mantissa[0]) - 1; i >= 0; i--) {
					carry = _addcarry_u64(carry, x.mantissa[i], 0, (long long unsigned int *) &x.mantissa[i]);
				}
#endif

				x.negative = true;
			}
		} else {
			if (carry) {
				x._ShiftRight(1);
				SET_MANTISSA_BIT(x, 0, 1);
			}
		}

		x._Normalize();

		return x;
	}

	BigFloat operator-(BigFloat y) {
		y.negative = !y.negative;
		return *this + y;
	}

	BigFloat operator*(BigFloat y) {
		// TODO Can this be done faster?
		if (zero) return *this;
		if (y.zero) return y;

		BigFloat accumulator = { .zero = true };

		for (intptr_t i = MANTISSA_BITS - 1; i >= 0; i--) {
			if (GET_MANTISSA_BIT(y, i)) {
				BigFloat place = *this;
				place.exponent = place.exponent - i + y.exponent;
				accumulator = accumulator + place;
			}
		}

		accumulator.negative = negative != y.negative;
		return accumulator;
	}
};

struct {
	BigFloat zero;
	BigFloat one;
	BigFloat two;
	BigFloat half;
	BigFloat halfPi;
	BigFloat pi;
	BigFloat twoPi;
	BigFloat log2;
	BigFloat e;
	BigFloat log2E;
} bigFloatConstants;

BigFloat BigFloatDivide(BigFloat x, BigFloat y, bool *error) {
	if (x.zero) return x;
	if (y.zero) { *error = true; return y; }

	BigFloat q = {};
	bool negative = x.negative != y.negative;
	x.negative = y.negative = false;

	intptr_t e = x.exponent - y.exponent;

	// Loop over MANTISSA_BITS + 1.
	// There will definitely be a subtraction in the first two iterations, by the initial choice of e.
	// Any additions to q with exponent less than initial e - MANTISSA_BITS - 1 therefore has no effect.

	for (int i = 0; i <= MANTISSA_BITS; i++) {
		BigFloat m = {};
		SET_MANTISSA_BIT(m, 0, 1);
		m.exponent = e;

		BigFloat s = y;
		s.exponent += e;
		BigFloat x1 = x - s;
		
		if (!x1.negative) {
			x = x1;
			q = q + m;
		}

		e--;
	}

	q.negative = negative;
	return q;
}

BigFloat BigFloatModulo(BigFloat x, BigFloat y, bool *error) {
	if (x.zero) return x;
	if (y.zero) { *error = true; return y; }

	x.negative = y.negative = false;
	intptr_t e = x.exponent - y.exponent;

	for (int i = 0; e >= 0; i++) {
		BigFloat m = {};
		SET_MANTISSA_BIT(m, 0, 1);
		m.exponent = e;

		BigFloat s = y;
		s.exponent += e;
		BigFloat x1 = x - s;
		
		if (!x1.negative) {
			x = x1;
		}

		e--;
	}

	return x;
}

BigFloat BigFloatRoundToNegativeInfinity(BigFloat x) {
	bool decrement = false;
	
	intptr_t start = x.exponent + 1;
	
	if (start < 0) {
		start = 0;
	}

	for (intptr_t i = start; i < MANTISSA_BITS; i++) {
		if (x.negative && !decrement && GET_MANTISSA_BIT(x, i)) {
			decrement = true;
		}

		SET_MANTISSA_BIT(x, i, 0);
	}

	x._Normalize();

	if (decrement) {
		return x - bigFloatConstants.one;
	} else {
		return x;
	}
}

BigFloat BigFloatSine(BigFloat x, bool *error) {
	// Get x in the range 0 to 2*pi.

	bool negate = x.negative;
	x.negative = false;
	x = BigFloatModulo(x, bigFloatConstants.twoPi, error);

	// Evaluate the Maclaurin series.

	if (error && *error) return x;
	if (x.zero) return x;

	BigFloat x2 = x * x;
	BigFloat y = {};
	y.zero = true;

	BigFloat p = x;
	BigFloat k = bigFloatConstants.two;

	for (int i = 0; i < 10000; i++) {
		if (p.exponent + MANTISSA_BITS < y.exponent && !y.zero) {
			break;
		}

		y = y + p;
		p.negative = !p.negative;
		p = x2 * BigFloatDivide(p, k * (k + bigFloatConstants.one), nullptr);
		k = k + bigFloatConstants.two;
	}

	if (negate) y.negative = !y.negative;
	return y;
}

BigFloat BigFloatCosine(BigFloat x, bool *error) {
	return BigFloatSine(bigFloatConstants.halfPi - x, error);
}

BigFloat BigFloatTangent(BigFloat x, bool *error) {
	return BigFloatDivide(BigFloatSine(x, error), BigFloatCosine(x, error), error);
}

BigFloat BigFloatLogarithm2(BigFloat x, bool *error) {
	if (x.negative || x.zero) {
		*error = true;
		return x;
	}
	
	BigFloat accumulator = {};
	accumulator._FromDouble(x.exponent);
	x.exponent = 0;
	
	BigFloat m = bigFloatConstants.one;
		
	do {
		if ((x - bigFloatConstants.one).zero) {
			break;
		}
		
		while ((x - bigFloatConstants.two).negative && accumulator.exponent - m.exponent < MANTISSA_BITS) {
			x = x * x;
			m.exponent--;
		}
		
		accumulator = accumulator + m;
		x.exponent--;
	} while (accumulator.exponent - m.exponent < MANTISSA_BITS);
	
	return accumulator;
}

BigFloat BigFloatExponential2(BigFloat x, bool *error) {
	BigFloat _integer = BigFloatRoundToNegativeInfinity(x);
	BigFloat fractional = x - _integer;
	int32_t integer = _integer.ToDouble();
	
	if (integer < -1000000 || integer > 1000000) {
		*error = true;
		return x;
	}
	
	BigFloat accumulator = {};
	accumulator.zero = true;
	BigFloat term = bigFloatConstants.one;
	BigFloat n = bigFloatConstants.one;
	
	do {
		accumulator = accumulator + term;
		term = BigFloatDivide(term * fractional * bigFloatConstants.log2, n, error);
		n = n + bigFloatConstants.one;
	} while (accumulator.exponent - term.exponent < MANTISSA_BITS && !term.zero);
	
	accumulator.exponent += integer;
	return accumulator;
}

BigFloat BigFloatPower(BigFloat x, BigFloat y, bool *error) {
	return BigFloatExponential2(y * BigFloatLogarithm2(x, error), error);
}

BigFloat BigFloatExponential(BigFloat x, bool *error) {
	return BigFloatExponential2(x * bigFloatConstants.log2E, error);
}

BigFloat BigFloatLogarithm(BigFloat x, bool *error) {
	return BigFloatDivide(BigFloatLogarithm2(x, error), bigFloatConstants.log2E, error);
}

BigFloat BigFloatArcSine(BigFloat x, bool *error) {
	BigFloat accumulator = {};
	accumulator.zero = true;
	BigFloat n = {};
	n.zero = true;
	BigFloat term = x;
	BigFloat xm = x * x;
	xm.exponent -= 2;
	BigFloat three = bigFloatConstants.one + bigFloatConstants.two;
	
	do {
		accumulator = accumulator + term;
		BigFloat n2 = n;
		n2.exponent++;
		BigFloat s1 = n2 + bigFloatConstants.one;
		BigFloat s2 = n + bigFloatConstants.one;
		term = BigFloatDivide(term * xm * (n2 + bigFloatConstants.two) * s1 * s1, s2 * s2 * (n2 + three), error);
		n = n + bigFloatConstants.one;
	} while (accumulator.exponent - term.exponent < MANTISSA_BITS && !term.zero);
	
	return accumulator;
}

BigFloat BigFloatArcCosine(BigFloat x, bool *error) {
	x.negative = !x.negative;
	return BigFloatArcSine(x, error) + bigFloatConstants.halfPi;
}

BigFloat BigFloatArcTangent(BigFloat x, bool *error) {
	return BigFloatArcSine(BigFloatDivide(x, BigFloatPower(bigFloatConstants.one + x * x, bigFloatConstants.half, error), error), error);
}

BigFloat BigFloatHyperbolicSine(BigFloat x, bool *error) {
	BigFloat ex = BigFloatExponential(x, error);
	return bigFloatConstants.half * (ex - BigFloatDivide(bigFloatConstants.one, ex, error));
}

BigFloat BigFloatHyperbolicCosine(BigFloat x, bool *error) {
	BigFloat ex = BigFloatExponential(x, error);
	return bigFloatConstants.half * (ex + BigFloatDivide(bigFloatConstants.one, ex, error));
}

BigFloat BigFloatHyperbolicTangent(BigFloat x, bool *error) {
	BigFloat e2x = BigFloatExponential(bigFloatConstants.two * x, error);
	return BigFloatDivide(e2x - bigFloatConstants.one, e2x + bigFloatConstants.one, error);
}

BigFloat BigFloatArcHyperbolicSine(BigFloat x, bool *error) {
	return BigFloatLogarithm(x + BigFloatPower(x * x + bigFloatConstants.one, bigFloatConstants.half, error), error);
}

BigFloat BigFloatArcHyperbolicCosine(BigFloat x, bool *error) {
	return BigFloatLogarithm(x + BigFloatPower(x * x - bigFloatConstants.one, bigFloatConstants.half, error), error);
}

BigFloat BigFloatArcHyperbolicTangent(BigFloat x, bool *error) {
	return bigFloatConstants.half * BigFloatLogarithm(BigFloatDivide(bigFloatConstants.one + x, bigFloatConstants.one - x, error), error);
}

void BigFloatInitialise() {
	EsMessageMutexCheck();

	static bool initialised = false;

	if (initialised) {
		return;
	}

	initialised = true;

	bigFloatConstants.zero.zero = true;
	bigFloatConstants.one.mantissa[0] = (uint64_t) 1 << 63;
	bigFloatConstants.one.exponent = 0;
	bigFloatConstants.two.mantissa[0] = (uint64_t) 1 << 63;
	bigFloatConstants.two.exponent = 1;
	bigFloatConstants.half.mantissa[0] = (uint64_t) 1 << 63;
	bigFloatConstants.half.exponent = -1;

	{
		BigFloat term = bigFloatConstants.two;
		BigFloat n = bigFloatConstants.one;
	
		BigFloat accumulator = {};
		accumulator.zero = true;
	
		while (term.exponent >= -MANTISSA_BITS) {
			accumulator = accumulator + term;
			term = term * BigFloatDivide(n * n, bigFloatConstants.two * n * n + n, nullptr);
			n = n + bigFloatConstants.one;
		}
	
		bigFloatConstants.pi = accumulator;
		bigFloatConstants.halfPi = BigFloatDivide(accumulator, bigFloatConstants.two, nullptr);
		bigFloatConstants.twoPi = bigFloatConstants.two * accumulator;
	}
	
	{
		BigFloat n = bigFloatConstants.one;
		
		BigFloat accumulator = {};
		accumulator.zero = true;

		for (int i = 1; i <= MANTISSA_BITS; i++) {
			BigFloat d = BigFloatDivide(bigFloatConstants.one, n, nullptr);
			d.exponent -= i;
			accumulator = accumulator + d;
			n = n + bigFloatConstants.one;
		}
		
		bigFloatConstants.log2 = accumulator;
	}
	
	{
		BigFloat n = bigFloatConstants.one;
		BigFloat term = bigFloatConstants.one;
		BigFloat accumulator = bigFloatConstants.one;

		for (int i = 0; i <= MANTISSA_BITS; i++) {
			accumulator = accumulator + term;
			n = n + bigFloatConstants.one;
			term = BigFloatDivide(term, n, nullptr);
		}
		
		bigFloatConstants.e = accumulator;
		bigFloatConstants.log2E = BigFloatLogarithm2(bigFloatConstants.e, nullptr);
	}
}

#endif

/////////////////////////////////
// Calculating expressions.
/////////////////////////////////

#ifdef SHARED_MATH_WANT_ALL

namespace Calculator {
	enum ValueType : uint8_t {
		VALUE_ERROR,
		VALUE_NUMBER,
	};

	struct Value {
		ValueType type;

		union {
			BigFloat number;
		};
	};

	enum TokenType : uint8_t {
		TOKEN_ERROR,
		TOKEN_PUNCTUATOR,
		TOKEN_LITERAL,
		TOKEN_IDENTIFIER,
		TOKEN_EOF,
	};

	struct Token {
		TokenType type;

		union {
			uint16_t punctuator;
			Value literal;
			char *identifier;
		};
	};

	enum NodeType : uint8_t {
		NODE_BINOP,
		NODE_UNARYOP,
		NODE_CALL,
		NODE_LITERAL,
	};

	struct Node {
		NodeType type;
		Node *firstChild, *sibling;
		Token token;
	};

	typedef Value (*Builtin)(Value *arguments, size_t argumentCount);

	struct Parser {
		const char *position;
		HashStore<char, Builtin> builtins;
#define CALCULATOR_PARSER_ALLOCATION_POOL_SIZE (8192)
		char *allocationPool;
		int allocatedBytes;

		void FreeAll() {
			allocatedBytes = 0;
			EsHeapFree(allocationPool);
			allocationPool = nullptr;
		}

		void *Allocate(size_t bytes) {
			bytes = (bytes + sizeof(max_align_t) - 1) & ~(sizeof(max_align_t) - 1);

			if (bytes + allocatedBytes > CALCULATOR_PARSER_ALLOCATION_POOL_SIZE) {
				return nullptr;
			}

			if (!allocationPool) {
				allocationPool = (char *) EsHeapAllocate(CALCULATOR_PARSER_ALLOCATION_POOL_SIZE, false);

				if (!allocationPool) {
					return nullptr;
				}
			}

			void *pointer = allocationPool + allocatedBytes;
			allocatedBytes += bytes;
			EsMemoryZero(pointer, bytes);
			return pointer;
		}

		Token PeekToken() {
			const char *oldPosition = position;
			Token token = NextToken();
			position = oldPosition;
			return token;
		}

		Token NextToken() {
			Token token = {};

			tryAgain:;

			unsigned char c = *position;

			if (c == '#') {
			        while (*position != '\n') position++;
			        goto tryAgain;
			} else if (EsCRTisspace(c)) {
			        position++;
			        goto tryAgain;
			}

			if ((c == '%' && position[1] == '%') || (c == '<' && position[1] == '=') || (c == '>' && position[1] == '=') 
			       	 || (c == '=' && position[1] == '=') || (c == '!' && position[1] == '=') 
			       	 || (c == '&' && position[1] == '&') || (c == '|' && position[1] == '|')
			       	 || (c == '/' && position[1] == '/')) {
			        token.type = TOKEN_PUNCTUATOR;
			        token.punctuator = (c << 8) + position[1];
			        position += 2;
			} else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '<' || c == '>' || c == '(' || c == ')' || c == '?' 
			       	 || c == ':' || c == ';' || c == ',' || c == '!' || c == '&' || c == '^') {
			        token.type = TOKEN_PUNCTUATOR;
			        token.punctuator = c;
			        position++;
			} else if (EsCRTisalpha(c) || c == '_' || c >= 0x80) {
			        const char *start = position;
			        while (EsCRTisalpha(c) || c == '_' || EsCRTisdigit(c) || c >= 0x80) c = *(++position);
			        token.type = TOKEN_IDENTIFIER;
			        token.identifier = (char *) Allocate(position - start + 1);
			        if (!token.identifier) return { TOKEN_ERROR };
			        EsMemoryCopy(token.identifier, start, position - start);
			        token.identifier[position - start] = 0;
			} else if (EsCRTisdigit(c) || c == '.') {
			        const char *start = position;
			        token.literal.number._FromDouble(EsDoubleParse(start, -1, (char **) &position));
			        token.type = TOKEN_LITERAL;
			        token.literal.type = VALUE_NUMBER;
			} else if (c == 0) {
			        token.type = TOKEN_EOF;
			} else {
			        token.type = TOKEN_ERROR;
			}

			return token;
		}

		Node *ParseExpression(int precedence = 0) {
			Node *left = (Node *) Allocate(sizeof(Node));
			if (!left) return nullptr;
			Token token = NextToken();
			left->token = token;

			if (token.type == TOKEN_LITERAL) {
				left->type = NODE_LITERAL;
			} else if (token.type == TOKEN_PUNCTUATOR) {
				left->type = NODE_UNARYOP;

				if (token.punctuator == '(') {
					left->firstChild = ParseExpression();
					token = NextToken();

					if (token.type != TOKEN_PUNCTUATOR || token.punctuator != ')' || !left->firstChild) {
						return nullptr;
					}
				} else if (token.punctuator == '!' || token.punctuator == '-' || token.punctuator == '/') {
					left->firstChild = ParseExpression(1000);

					if (!left->firstChild) {
						return nullptr;
					}
				} else {
					return nullptr;
				}
			} else if (token.type == TOKEN_IDENTIFIER) {
				Token peek = PeekToken();

				if (peek.type == TOKEN_PUNCTUATOR && peek.punctuator == '(') {
					left->type = NODE_CALL;
					NextToken();
					int count = 0;
					Node **link = &left->firstChild;

					while (true) {
						token = PeekToken();

						if (token.type == TOKEN_PUNCTUATOR && token.punctuator == ')') {
							NextToken();
							break;
						} else if (count) {
							if (token.type != TOKEN_PUNCTUATOR || token.punctuator != ',') {
								return nullptr;
							}

							NextToken();
						}

						*link = ParseExpression();

						if (!(*link)) {
							return nullptr;
						}

						link = &((*link)->sibling);
						count++;
					}
				} else if (0 == EsCRTstrcmp(token.identifier, "\xCF\x80") || 0 == EsCRTstrcmp(token.identifier, "pi")) {
					left->type = NODE_LITERAL;
					left->token.literal.type = VALUE_NUMBER;
					left->token.literal.number = bigFloatConstants.pi;
				} else if (0 == EsCRTstrcmp(token.identifier, "\xCF\x84") || 0 == EsCRTstrcmp(token.identifier, "tau")) {
					left->type = NODE_LITERAL;
					left->token.literal.type = VALUE_NUMBER;
					left->token.literal.number = bigFloatConstants.twoPi;
				} else if (0 == EsCRTstrcmp(token.identifier, "e")) {
					left->type = NODE_LITERAL;
					left->token.literal.type = VALUE_NUMBER;
					left->token.literal.number = bigFloatConstants.e;
				} else {
					return nullptr;
				}
			} else {
				return nullptr;
			}

			while (true) {
				token = PeekToken();

				if (0) {

#define PARSE_BINOP(symb, prec) \
				} else if (token.type == TOKEN_PUNCTUATOR && token.punctuator == symb && prec > precedence) { \
					Node *node = (Node *) Allocate(sizeof(Node)); \
					if (!node) return nullptr; \
					node->type = NODE_BINOP; \
					node->token = NextToken(); \
					node->firstChild = left; \
					left->sibling = ParseExpression(prec); \
					if (!left->sibling) return nullptr; \
					left = node

					PARSE_BINOP('^', 120);
					PARSE_BINOP('*', 100);
					PARSE_BINOP('/', 100);
					PARSE_BINOP('+', 80);
					PARSE_BINOP('-', 80);
				} else {
					break;
				}
			}

			return left;
		}

		Value Evaluate(Node *node, Value *arguments) {
			if (node->type == NODE_LITERAL) {
				return node->token.literal;
			} else if (node->type == NODE_CALL) {
#define MAX_CALCULATOR_CALL_ARGUMENTS (16)
				Value callArguments[MAX_CALCULATOR_CALL_ARGUMENTS];
				int argumentCount = 0;
				Node *child = node->firstChild;

				while (child) {
					callArguments[argumentCount] = Evaluate(child, arguments);

					if (callArguments[argumentCount].type == VALUE_ERROR) {
						return {};
					}

					child = child->sibling;
					argumentCount++;
					if (argumentCount == MAX_CALCULATOR_CALL_ARGUMENTS) return {};
				}

				if (!builtins.Count()) LoadBuiltins();
				Builtin builtin = builtins.Get1(node->token.identifier, EsCStringLength(node->token.identifier));
				if (!builtin) return {};
				return builtin(callArguments, argumentCount);
			} else if (node->type == NODE_UNARYOP) {
				Value value = Evaluate(node->firstChild, arguments);

				if (node->token.punctuator == '(') {
					return value;
				} else if (node->token.punctuator == '!') {
					if (value.type != VALUE_NUMBER) return {};
					return { VALUE_NUMBER, .number = value.number.zero ? bigFloatConstants.one : bigFloatConstants.zero };
				} else if (node->token.punctuator == '-') {
					if (value.type != VALUE_NUMBER) return {};
					value.number.negative = !value.number.negative;
					return value;
				} else if (node->token.punctuator == '/') {
					if (value.type != VALUE_NUMBER) return {};
					bool error = false;
					value.number = BigFloatDivide(bigFloatConstants.one, value.number, &error);
					if (error) value.type = VALUE_ERROR;
					return value;
				} else {
					return {};
				}
			} else if (node->type == NODE_BINOP) {
				Value left = Evaluate(node->firstChild, arguments),
				      right = Evaluate(node->firstChild->sibling, arguments);

				if (0) {

#define DO_BINOP(p, t, output, expression) \
				} else if (node->token.punctuator == p) { \
					if (left.type != t || right.type != t) return {}; \
					Value result = { t }; \
					bool error = false; \
					output = expression; \
					if (error) result.type = VALUE_ERROR; \
					return result

					DO_BINOP('+', VALUE_NUMBER, result.number, left.number + right.number);
					DO_BINOP('-', VALUE_NUMBER, result.number, left.number - right.number);
					DO_BINOP('*', VALUE_NUMBER, result.number, left.number * right.number);
					DO_BINOP('/', VALUE_NUMBER, result.number, BigFloatDivide(left.number, right.number, &error));
					DO_BINOP('^', VALUE_NUMBER, result.number, BigFloatPower(left.number, right.number, &error));
#undef DO_BINOP
				} else {
					return {};
				}
			} else {
				return {};
			}
		}

		void LoadBuiltins() {
#define FUNCTION1(name, callback) \
			do { Builtin builtin = [] (Value *arguments, size_t argumentCount) { \
				if (argumentCount != 1 || arguments[0].type != VALUE_NUMBER) return (Value) {}; \
				Value result = { VALUE_NUMBER }; \
				BigFloat x = arguments[0].number; \
				bool error = false; \
				result.number = callback; \
				if (error) result.type = VALUE_ERROR; \
				return result; \
			}; *builtins.Put(name, EsCStringLength(name)) = builtin; } while (0)

#define FUNCTION2(name, callback) \
			do { Builtin builtin = [] (Value *arguments, size_t argumentCount) { \
				if (argumentCount != 2 || arguments[0].type != VALUE_NUMBER || arguments[1].type != VALUE_NUMBER) return (Value) {}; \
				Value result = { VALUE_NUMBER }; \
				BigFloat x = arguments[0].number, y = arguments[1].number; \
				bool error = false; \
				result.number = callback; \
				if (error) result.type = VALUE_ERROR; \
				return result; \
			}; *builtins.Put(name, EsCStringLength(name)) = builtin; } while (0)

			FUNCTION1("sq", BigFloatPower(x, bigFloatConstants.two, &error));
			FUNCTION1("sqrt", BigFloatPower(x, bigFloatConstants.half, &error));
			FUNCTION1("ln", BigFloatLogarithm(x, &error));
			FUNCTION1("sin", BigFloatSine(x, &error));
			FUNCTION1("sinh", BigFloatHyperbolicSine(x, &error));
			FUNCTION1("cos", BigFloatCosine(x, &error));
			FUNCTION1("cosh", BigFloatHyperbolicCosine(x, &error));
			FUNCTION1("tan", BigFloatTangent(x, &error));
			FUNCTION1("tanh", BigFloatHyperbolicTangent(x, &error));
			FUNCTION1("asin", BigFloatArcSine(x, &error));
			FUNCTION1("asinh", BigFloatArcHyperbolicSine(x, &error));
			FUNCTION1("acos", BigFloatArcCosine(x, &error));
			FUNCTION1("acosh", BigFloatArcHyperbolicCosine(x, &error));
			FUNCTION1("atan", BigFloatArcTangent(x, &error));
			FUNCTION1("atanh", BigFloatArcHyperbolicTangent(x, &error));
			FUNCTION2("mod", BigFloatModulo(x, y, &error));
			FUNCTION2("rt", BigFloatPower(x, BigFloatDivide(bigFloatConstants.one, y, &error), &error));
			FUNCTION2("log", BigFloatDivide(BigFloatLogarithm2(y, &error), BigFloatLogarithm2(x, &error), &error));

#undef FUNCTION1
#undef FUNCTION2
		}

		void Free() {
			FreeAll();
			builtins.Free();
		}
	};
};

Calculator::Parser calculator = {};

EsCalculationValue EsCalculateFromUserExpression(const char *expression) {
	EsMessageMutexCheck();

	BigFloatInitialise();

	calculator.FreeAll();
	calculator.position = expression;
	Calculator::Node *node = calculator.ParseExpression();
	Calculator::Value value = {};
	if (calculator.NextToken().type != Calculator::TOKEN_EOF) node = nullptr;
	if (node) value = calculator.Evaluate(node, nullptr);

	EsCalculationValue result = {};
	result.error = value.type == Calculator::VALUE_ERROR;
	result.number = value.type == Calculator::VALUE_NUMBER ? value.number.ToDouble() : 0;
	return result;
}

#endif
