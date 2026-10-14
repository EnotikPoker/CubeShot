# Invoked after linking Windows executables. Empty for fully static dependencies.
foreach(dll IN LISTS DLLS)
    file(COPY "${dll}" DESTINATION "${DESTINATION}")
endforeach()
