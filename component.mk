#
# Main Makefile. This is basically the same as a component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_ADD_INCLUDEDIRS = .

ifdef CONFIG_IDF_TARGET_ESP8266
COMPONENT_DEPENDS = esp8266 freertos
else
COMPONENT_DEPENDS = freertos
endif
