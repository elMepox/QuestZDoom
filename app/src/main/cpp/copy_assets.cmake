# usage: cmake -DPK3_DIR=<dir with built *.pk3> -DUZDOOM_SRC=<UZDoom source> -DDST=<asset dir> -P copy_assets.cmake
file(MAKE_DIRECTORY "${DST}/soundfonts" "${DST}/fm_banks")
file(GLOB _pk3s "${PK3_DIR}/*.pk3")
foreach(_f IN LISTS _pk3s)
  file(COPY "${_f}" DESTINATION "${DST}")
endforeach()
file(COPY "${UZDOOM_SRC}/soundfont/uzdoom.sf2" DESTINATION "${DST}/soundfonts")
file(COPY "${UZDOOM_SRC}/fm_banks/GENMIDI.GS.wopl" "${UZDOOM_SRC}/fm_banks/gs-by-papiezak-and-sneakernets.wopn"
     DESTINATION "${DST}/fm_banks")
