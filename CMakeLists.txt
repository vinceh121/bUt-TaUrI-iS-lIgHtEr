cmake_minimum_required(VERSION 2.8.12)

include(GNUInstallDirs)
include(FindPkgConfig)

project(buttauriislighter DESCRIPTION "bUt TaUrI iS lIgHtEr" LANGUAGES C)

add_executable(buttauriislighter src/btil.c)

pkg_check_modules(GTK REQUIRED gtk+-3.0)
target_link_libraries(buttauriislighter ${GTK_LDFLAGS})
target_compile_options(buttauriislighter PRIVATE ${GTK_CFLAGS})

pkg_check_modules(WK REQUIRED webkit2gtk-4.1)
target_link_libraries(buttauriislighter ${WK_LDFLAGS})
target_compile_options(buttauriislighter PRIVATE ${WK_CFLAGS})

pkg_check_modules(CURL REQUIRED libcurl)
target_link_libraries(buttauriislighter ${CURL_LDFLAGS})
target_compile_options(buttauriislighter PRIVATE ${CURL_CFLAGS})

pkg_check_modules(JSON REQUIRED jansson)
target_link_libraries(buttauriislighter ${JSON_LDFLAGS})
target_compile_options(buttauriislighter PRIVATE ${JSON_CFLAGS})

configure_file("src/Wizard.glade" "Wizard.glade" COPYONLY)
configure_file("src/Consent.glade" "Consent.glade" COPYONLY)

install(TARGETS buttauriislighter RUNTIME)

