# MenuList's 64-bit load: the generated steps and its custom record body, with
# Menu's real steps (and Material's, TechniqueSet's, Image's and the sound
# families', which Menu's backgrounds and focus sounds name) for the menus.
kisakcod_disk32_load_test(menulist db_disk32_menulist_tests.cpp db_disk32_menulist.cpp db_disk32_menu.cpp
    db_disk32_material.cpp db_disk32_techniqueset.cpp db_disk32_image.cpp db_disk32_sound.cpp
    db_disk32_loadedsound.cpp db_disk32_soundcurve.cpp)
