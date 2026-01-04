# UASP SSD Full TRIM Tool - Makefile for nmake
#
# Usage:
#   nmake          - Build release version
#   nmake debug    - Build debug version
#   nmake clean    - Clean build artifacts
#

CC = cl
RC = rc
LINK = link

TARGET = uasp_trim.exe
SOURCES = main.cpp
RESOURCES = uasp_trim.rc
RES_OBJ = uasp_trim.res

LIBS = advapi32.lib

# Release flags
CFLAGS = /nologo /EHsc /W4 /O2 /DNDEBUG
LDFLAGS = /nologo

# Debug flags
CFLAGS_DEBUG = /nologo /EHsc /W4 /Od /Zi /DDEBUG
LDFLAGS_DEBUG = /nologo /DEBUG

all: $(TARGET)

$(RES_OBJ): $(RESOURCES) uasp_trim.manifest
	$(RC) /nologo $(RESOURCES)

$(TARGET): $(SOURCES) $(RES_OBJ)
	$(CC) $(CFLAGS) $(SOURCES) $(RES_OBJ) /Fe:$(TARGET) /link $(LDFLAGS) $(LIBS)

debug: $(SOURCES) $(RES_OBJ)
	$(CC) $(CFLAGS_DEBUG) $(SOURCES) $(RES_OBJ) /Fe:$(TARGET) /link $(LDFLAGS_DEBUG) $(LIBS)

clean:
	-del /Q $(TARGET) *.obj *.res *.pdb *.ilk 2>nul

.PHONY: all debug clean
