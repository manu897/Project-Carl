add_custom_target(b0n_shared_property_target)
set_property(TARGET b0n_shared_property_target  PROPERTY KERNEL_HEX_NAME;zephyr.hex)
set_property(TARGET b0n_shared_property_target  PROPERTY ZEPHYR_BINARY_DIR;C:/Users/manid/Downloads/Project-Carl/build/empty_net_core/b0n/zephyr)
set_property(TARGET b0n_shared_property_target  PROPERTY KERNEL_ELF_NAME;zephyr.elf)
set_property(TARGET b0n_shared_property_target  PROPERTY BUILD_BYPRODUCTS;C:/Users/manid/Downloads/Project-Carl/build/empty_net_core/b0n/zephyr/zephyr.hex;C:/Users/manid/Downloads/Project-Carl/build/empty_net_core/b0n/zephyr/zephyr.elf)
set_property(TARGET b0n_shared_property_target APPEND PROPERTY PM_YML_DEP_FILES;C:/ncs/v2.5.99-dev1/nrf/samples/nrf5340/netboot/pm.yml)
set_property(TARGET b0n_shared_property_target APPEND PROPERTY PM_YML_FILES;C:/Users/manid/Downloads/Project-Carl/build/empty_net_core/b0n/zephyr/include/generated/pm.yml)
set_property(TARGET b0n_shared_property_target APPEND PROPERTY PM_YML_DEP_FILES;C:/ncs/v2.5.99-dev1/nrf/subsys/partition_manager/pm.yml.secure_boot_storage)
set_property(TARGET b0n_shared_property_target APPEND PROPERTY PM_YML_FILES;C:/Users/manid/Downloads/Project-Carl/build/empty_net_core/b0n/modules/nrf/subsys/partition_manager/pm.yml.secure_boot_storage)
