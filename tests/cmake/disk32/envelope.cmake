# The XAssetList/XAsset envelope, dispatching to the RawFile loader;
# Load_XAsset's family switch is replaced.
kisakcod_disk32_load_test(envelope db_disk32_envelope_tests.cpp
    db_disk32_envelope.cpp db_disk32_load.cpp db_xasset_disk32.cpp db_stringtable_load.cpp db_asset_layout.cpp)
