section .data

global mbr_data
mbr_data:
%ifdef MBR_PATH
incbin MBR_PATH
%endif

global mbr_size
mbr_size: dd $ - mbr_data

global stage2_data
stage2_data:
incbin STAGE2_PATH

global stage2_size
stage2_size: dd $ - stage2_data
