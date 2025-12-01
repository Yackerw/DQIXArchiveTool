FROM emscripten/emsdk:3.1.56

WORKDIR /work
COPY .. /work

# Build the WebAssembly module into /public. This image build itself produces the artifacts,
# but for iterative local builds on Windows, prefer using compile.bat which mounts the repo.
RUN mkdir -p /work/public && \
    emcc \
      ArchiveTool/CompressA.cpp \
      ArchiveTool/CompressB.cpp \
      ArchiveTool/CompressC.cpp \
      ArchiveTool/Reader.cpp \
      ArchiveTool/gp2.cpp \
      ArchiveTool/wasm_bridge.cpp \
      -std=c++17 -O3 \
      -sASSERTIONS=1 \
      -sENVIRONMENT=web \
      -sALLOW_MEMORY_GROWTH=1 \
      -sFILESYSTEM=1 \
      -sEXPORTED_FUNCTIONS=['_ProcessFile'] \
      -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
      -sNO_EXIT_RUNTIME=1 \
      -sINVOKE_RUN=0 \
      -o /work/public/gp2.js

# This container is not a server; it only performs the build when built.
CMD ["bash","-lc","echo Built /public/gp2.js and gp2.wasm inside the image. Use compile.bat for local volume-mounted builds."]
