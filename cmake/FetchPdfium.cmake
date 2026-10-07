# SPDX-License-Identifier: GPL-3.0-only
# Explicit, checksum-pinned development dependency. Never fetched at configure time.
set(package_url "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8086/pdfium-v8-linux-x64.tgz")
set(package_sha256 "8e3efa4fe6784ed80fd58ef3c839b072ddcd79b16880706892a008f8a2da730b")
if(NOT DEFINED PDFIUM_DESTINATION)
    set(PDFIUM_DESTINATION "${CMAKE_CURRENT_LIST_DIR}/../.deps/pdfium")
endif()
get_filename_component(PDFIUM_DESTINATION "${PDFIUM_DESTINATION}" ABSOLUTE)
file(MAKE_DIRECTORY "${PDFIUM_DESTINATION}")
set(archive "${PDFIUM_DESTINATION}/pdfium-v8-linux-x64.tgz")
file(DOWNLOAD "${package_url}" "${archive}"
    EXPECTED_HASH "SHA256=${package_sha256}" TLS_VERIFY ON STATUS result)
list(GET result 0 code)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "PDFium download failed: ${result}")
endif()
file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${PDFIUM_DESTINATION}")
file(REMOVE "${archive}")
message(STATUS "PDFium XFA/V8 157.0.8086.0 extracted to ${PDFIUM_DESTINATION}")
