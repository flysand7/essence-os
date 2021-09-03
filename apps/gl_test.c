// gcc -o tri tri.c -lOSMesa && ./tri
// x86_64-essence-gcc -o root/tri ports/mesa/tri.c -lOSMesa -lstdc++ -lz -g -D ESSENCE_WINDOW -D MODERN_GL

#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>

#define IMAGE_WIDTH (700)
#define IMAGE_HEIGHT (600)
uint32_t *buffer;

#ifdef MODERN_GL
static GLenum (*glCheckFramebufferStatus)(GLenum target);
static GLint (*glGetUniformLocation)(GLuint program, const GLchar *name);
static GLuint (*glCreateProgram)();
static GLuint (*glCreateShader)(GLenum type);
static void (*glAttachShader)(GLuint program, GLuint shader);
static void (*glBindBuffer)(GLenum target, GLuint buffer);
static void (*glBindFramebuffer)(GLenum target, GLuint framebuffer);
static void (*glBindVertexArray)(GLuint array);
static void (*glBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
static void (*glCompileShader)(GLuint shader);
static void (*glDeleteFramebuffers)(GLsizei n, const GLuint *framebuffers);
static void (*glDrawBuffers)(GLsizei n, const GLenum *bufs);
static void (*glEnableVertexAttribArray)();
static void (*glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
static void (*glGenBuffers)(GLsizei n, GLuint *buffers);
static void (*glGenFramebuffers)(GLsizei n, GLuint *framebuffers);
static void (*glGenVertexArrays)(GLsizei n, GLuint *arrays);
static void (*glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static void (*glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static void (*glGetShaderiv)(GLuint shader, GLenum pname, GLint *param);
static void (*glLinkProgram)(GLuint program);
static void (*glShaderSource)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
static void (*glUniform1f)(GLint location, GLfloat v0);
static void (*glUniform1i)(GLint location, GLint v0);
static void (*glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
static void (*glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static void (*glUseProgram)(GLuint program);
static void (*glValidateProgram)(GLuint program);
static void (*glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

#define GL_CLAMP_TO_EDGE 0x812F
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_MULTISAMPLE 0x809D
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_BGRA 0x80E1
#endif

#ifdef ESSENCE_WINDOW
#include <essence.h>

int CanvasCallback(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT_BACKGROUND) {
		EsRectangle bounds = EsRectangleCenter(EsPainterBoundsInset(message->painter), ES_RECT_2S(IMAGE_WIDTH, IMAGE_HEIGHT));
		EsDrawBitmap(message->painter, bounds, buffer, IMAGE_WIDTH * 4, ES_DRAW_BITMAP_OPAQUE);
	}

	return 0;
}
#endif

int main(int argc, char **argv) {
#ifndef MODERN_GL
	OSMesaContext context = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
	buffer = (uint32_t *) malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);
	OSMesaMakeCurrent(context, buffer, GL_UNSIGNED_BYTE, IMAGE_WIDTH, IMAGE_HEIGHT);

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glLoadIdentity();
	glBegin(GL_TRIANGLES);
	glColor4f(1, 0, 0, 1);
	glVertex2f(-1, -1);
	glColor4f(0, 1, 0, 1);
	glVertex2f(0, 1);
	glColor4f(0, 0, 1, 1);
	glVertex2f(1, -1);
	glEnd();
#else
	const int contextAttributes[] = {
		OSMESA_FORMAT, OSMESA_RGBA,
		OSMESA_DEPTH_BITS, 16,
		OSMESA_PROFILE, OSMESA_CORE_PROFILE,
		OSMESA_CONTEXT_MAJOR_VERSION, 3,
		OSMESA_CONTEXT_MINOR_VERSION, 0,
		0
	};

	OSMesaContext context = OSMesaCreateContextAttribs(contextAttributes, NULL);
	buffer = (uint32_t *) malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);
	OSMesaMakeCurrent(context, buffer, GL_UNSIGNED_BYTE, IMAGE_WIDTH, IMAGE_HEIGHT);

#define LOADEXT(x) *(void **) &x = (void *) OSMesaGetProcAddress(#x); assert(x);
	LOADEXT(glAttachShader);
	LOADEXT(glBindBuffer);
	LOADEXT(glBindFramebuffer);
	LOADEXT(glBindVertexArray);
	LOADEXT(glBufferData);
	LOADEXT(glCheckFramebufferStatus);
	LOADEXT(glCompileShader);
	LOADEXT(glCreateProgram);
	LOADEXT(glCreateShader);
	LOADEXT(glDeleteFramebuffers);
	LOADEXT(glDrawBuffers);
	LOADEXT(glEnableVertexAttribArray);
	LOADEXT(glFramebufferTexture2D);
	LOADEXT(glGenBuffers);	
	LOADEXT(glGenFramebuffers);
	LOADEXT(glGenVertexArrays);
	LOADEXT(glGetProgramInfoLog);
	LOADEXT(glGetShaderInfoLog);
	LOADEXT(glGetShaderiv);
	LOADEXT(glGetUniformLocation);
	LOADEXT(glLinkProgram);	
	LOADEXT(glShaderSource);
	LOADEXT(glUniform1f);
	LOADEXT(glUniform1i);
	LOADEXT(glUniform4f);
	LOADEXT(glUniformMatrix4fv);
	LOADEXT(glUseProgram);
	LOADEXT(glValidateProgram);
	LOADEXT(glVertexAttribPointer);
#undef LOADEXT

	glClearColor(0, 0, 0, 1);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_MULTISAMPLE);

	int squareVBO, squareIBO;
	float squareVBOArray[] = { -1, -1, 0.5f, /**/ -1, 1, 0.5f, /**/ 1, 1, 0.5f, /**/ 1, -1, 0.5f };
	unsigned squareIBOArray[] = { 0, 1, 2, 0, 2, 3 };
	glGenBuffers(1, &squareVBO);
	glBindBuffer(GL_ARRAY_BUFFER, squareVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(squareVBOArray), squareVBOArray, GL_STATIC_DRAW);
	glGenBuffers(1, &squareIBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, squareIBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(squareIBOArray), squareIBOArray, GL_STATIC_DRAW);

	const char *vertexShaderSource = 
		"#version 330\n"
		"layout(location = 0) in vec3 Position;\n"
		"uniform mat4 transform;\n"
		"out vec2 TexCoord0;\n"
		"void main() { \n"
		"	gl_Position = transform * vec4(Position, 1.0);\n"
		"}\n";
	const char *fragmentShaderSource = 
		"#version 330\n"
		"layout(location = 0) out vec4 FragColor;\n"
		"in vec2 TexCoord0;\n"
		"uniform vec4 blendColor;\n"
		"void main() { \n"
		"	FragColor = blendColor;\n"
		"}\n";

	const char *shaderSources[] = { vertexShaderSource, fragmentShaderSource };
	int shaderSourceLengths[] = { strlen(vertexShaderSource), strlen(fragmentShaderSource) };

	unsigned shader = glCreateProgram();
	char shaderInfoLog[1024];

	unsigned vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, shaderSources + 0, shaderSourceLengths + 0);
	glCompileShader(vertexShader);
	glGetShaderInfoLog(vertexShader, sizeof(shaderInfoLog), NULL, shaderInfoLog);
	glAttachShader(shader, vertexShader);	
	printf("Vertex shader log: '%s'\n", shaderInfoLog);

	unsigned fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, shaderSources + 1, shaderSourceLengths + 1);
	glCompileShader(fragmentShader);
	glGetShaderInfoLog(fragmentShader, sizeof(shaderInfoLog), NULL, shaderInfoLog);
	glAttachShader(shader, fragmentShader);	
	printf("Fragment shader log: '%s'\n", shaderInfoLog);

	glLinkProgram(shader);
	glValidateProgram(shader);

	int shaderBlendColor = glGetUniformLocation(shader, "blendColor");
	int shaderTransform = glGetUniformLocation(shader, "transform");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	unsigned vertexArray;
	glGenVertexArrays(1, &vertexArray);
	glBindVertexArray(vertexArray);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, squareVBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, squareIBO);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (const GLvoid *) 0);
	glClear(GL_COLOR_BUFFER_BIT); 

	glUseProgram(shader);
	float transform[] = { 0.5f, 0, 0, 0, /**/ 0, 0.5f, 0, 0, /**/ 0, 0, 1, 0, /**/ 0, 0, 0, 1 };
	glUniformMatrix4fv(shaderTransform, 1, GL_TRUE, transform);
	glUniform4f(shaderBlendColor, 1, 0, 1, 1);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
#endif

	glFinish();
	
#ifndef ESSENCE_WINDOW
	FILE *out = fopen("test.ppm", "wb");
	fprintf(out, "P6\n%d %d\n255\n", IMAGE_WIDTH, IMAGE_HEIGHT);

	for (int j = 0; j < IMAGE_HEIGHT; j++) {
		for (int i = 0; i < IMAGE_WIDTH; i++) {
			fwrite(buffer + j * IMAGE_WIDTH + i, 1, 3, out);
		}
	}

	fclose(out);
	OSMesaDestroyContext(context);
	free(buffer);
#else
	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, "GL Test", -1);
			EsWindowSetTitle(instance->window, "GL Test", -1);
			EsCustomElementCreate(instance->window, ES_CELL_FILL, 0)->messageUser = (EsUICallback) CanvasCallback;
		}
	}
#endif

	return 0;
}
