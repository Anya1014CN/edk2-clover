function platform_build_bootimg(){
	# Match the verified legacy Axon M Android boot header layout collected via
	# read-only adb on 2026-09-06.  The output file keeps the historical
	# boot-fujisan.img name but is signed for and intended for the recovery
	# partition. The UEFI payload is still a test kernel.
	local UNSIGNED="${OUTDIR}/boot-${DEVICE}${EXT}.img.unsigned"
	python3 "${ROOTDIR}/tools/mkbootimg.py" \
		--kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		--ramdisk ramdisk \
		--base "${BOOTIMG_BASE}" \
		--kernel_offset "${BOOTIMG_KERNEL_OFFSET}" \
		--ramdisk_offset "${BOOTIMG_RAMDISK_OFFSET}" \
		--tags_offset "${BOOTIMG_TAGS_OFFSET}" \
		--pagesize "${BOOTIMG_PAGESIZE}" \
		--os_version "${BOOTIMG_OS_VERSION}" \
		--os_patch_level "${BOOTIMG_OS_PATCH_LEVEL}" \
		--header_version "${BOOTIMG_HEADER_VERSION}" \
		-o "${UNSIGNED}" \
		||return "$?"
	python3 "${ROOTDIR}/tools/boot_signature.py" \
		--partition /recovery \
		--input "${UNSIGNED}" \
		--key "${ROOTDIR}/Platform/${VENDOR_NAME}/${SOC_PLATFORM_L}/keys/verity.pk8" \
		--certificate "${ROOTDIR}/Platform/${VENDOR_NAME}/${SOC_PLATFORM_L}/keys/verity.x509.pem" \
		--output "${OUTDIR}/boot-${DEVICE}${EXT}.img" \
		||return "$?"
	rm -f "${UNSIGNED}"
}
