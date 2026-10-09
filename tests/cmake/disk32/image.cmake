# Image's 64-bit load: the generated steps and its custom record body.
kisakcod_disk32_load_test(image db_disk32_image_tests.cpp db_disk32_image.cpp)
# The same load as a client: it creates textures and shares aliased ones.
kisakcod_disk32_load_test(image-client db_disk32_image_tests.cpp db_disk32_image.cpp)
