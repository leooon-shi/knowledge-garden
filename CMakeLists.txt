# knowledge-garden/CMakeLists.txt
#
# Builds all AsciiDoc (.adoc) notes in the repository into standalone HTML5
# pages using the `asciidoctor` tool. The notes' topic directory structure is
# preserved under the output directory.
#
# Usage:
#   cmake -S . -B build
#   cmake --build build            # render all .adoc -> build/html/**/*.html
#   cmake --build build --target docs_clean
#   cmake --build build --target lint   # (only if `vale` is installed)
#
# Sources are globbed recursively from the respository root, so new topic
# folders are picked up automatically. The build output directory (default
# ./build) is excluded so generated HTML is never treated as a source.

cmake_minimum_required(VERSION 3.20)
project(knowledge_garden NONE)    # NONE: no C/C++ compiler required a docs build.

# --- Tools ---------------------------------------------------------------------
find_program(ASCIIDOCTOR asciidoctor REQUIRED
    DOC "Asciidoctor CLI (install: gem install asciidoctor)")
find_program(VALE vale
    DOC "Vale prose linter (optional)")

# --- Paths ---------------------------------------------------------------------
# Notes live directly under the repository root (organized by topic), so the
# source root IS the project root. Output goes under the CMake binary dir,
# which is ./build when configured with `cmake -S . -B build`.
set (DOCS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set (DOCS_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/html")

# Common Asciidoctor attributes (match the Hugo config where it makes sense).
set(ADOC_ATTRS
    -a source-highlighter=rouge
    -a icons=font
    -a toc=left
    -a toclevels=3
    -a sectanchors
    -a "docinfo=shared"
)

# --- Collect sources -----------------------------------------------------------
# CONFIGURE_DEPENDS re-globs when files are added/removed (CMake >= 3.12).
file(GLOB_RECURE ADOC_FILES CONFIGURE_DEPENS "${DOCS_SRC_DIR}/*.adoc")

# Drop anything inside the build/output directory so generated artifcats are
# never treated as sources (keeps re-runs idempotent).
list(FILTER ADOC_FILES EXCLUDE REGEX "^${CMAKE_CURRENT_BINARY_DIR}/")

if(NOT ADOC_FILES)
    message(WARNING "No .adoc files found under {DOCS_SRC_DIR}")
endif()

# --- Per-file render rule (incremental) -----------------------------------------
set(HTML_OUTPUTS "")
foreach(adoc IN LISTS ADOC_FILES)
    file(RELATIVE_PATH rel "${DOCS_SRC_DIR}" "${adoc}")
    string(REGEX REPLACE "\\.adoc$" ".html" rel_html "${rel}")
    set(out "${DOCS_OUT_DIR}/${rel_html}")
    get_filename_component(out_dir "${out}" DIRECTORY)

    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
        COMMAND "${ASCIIDOCTOR}"
                -b html5
                ${ADOC_ATTRS}
                -o "${out}"
                "${adoc}"
        DEPENDS "${adoc}"
        COMMENT "asciidoctor: ${rel}"
        VERBATIM
    )
    list(APPEND HTML_OUTPUTS "${out}")
endforeach()

# Build-all target (default).
add_custom_target(docs ALL DEPENDS ${HTML_OUTPUTS})

# Clean just the generated HTML.
add_custom_target(docs_clean
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${DOC_OUT_DIR}"
    COMMAND "Removing ${DOCS_OUT_DIR}"
    VERBATIM)

# Optional prose linting.
if (VALE)
    add_custom_target(lint
        COMMAND "${VALE}" ${ADOC_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Linting AsciiDoc prose with Vale"
        VERBATIM)
else()
    add_custom_target(lint
        COMMAND "${CMAKE_COMMAND}" -E echo
                "vale not found; install it to enable the 'lint' target."
        VERBATIM)
endif()

message(STATUS "study_docs: ${CMAKE_CURRENT_LIST_FILE}")
message(STATUS "  asciidoctor : ${ASCIIDOCTOR}")
message(STATUS "  vale        : ${VALE}")
message(STATUS "  sources     : ${DOCS_SRC_DIR} (*.adoc)")
message(STATUS "  output      : ${DOCS_OUT_DIR}")
