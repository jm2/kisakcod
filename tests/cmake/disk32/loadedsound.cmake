# LoadedSound's 64-bit load: the generated steps and its custom record body.
kisakcod_disk32_load_test(loadedsound db_disk32_loadedsound_tests.cpp db_disk32_loadedsound.cpp)
# The same load as a client: it hands each sound's bytes to SND_SetData.
kisakcod_disk32_load_test(loadedsound-client db_disk32_loadedsound_tests.cpp db_disk32_loadedsound.cpp)
