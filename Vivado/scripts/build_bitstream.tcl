# Copyright (C) 2022 Xilinx, Inc
# SPDX-License-Identifier: BSD-3-Clause

set board_name "pynq_z1"
set top_vhd_file "pynq_z1_top"

# open block design
open_project ./${board_name}/${board_name}.xpr
open_bd_design ./${board_name}/${board_name}.srcs/sources_1/bd/${board_name}/${board_name}.bd

# add top wrapper and xdc files
make_wrapper -files [get_files ./${board_name}/${board_name}.srcs/sources_1/bd/${board_name}/${board_name}.bd] -top
#add_files -norecurse ./${board_name}/${board_name}.srcs/sources_1/bd/${board_name}/hdl/${board_name}_wrapper.vhd
add_files -norecurse ./src/${top_vhd_file}.vhd
set_property top ${board_name}_wrapper [current_fileset]
import_files -fileset constrs_1 -norecurse ./src/${board_name}.xdc
update_compile_order -fileset sources_1

# set platform property
set_property platform.default_output_type "sd_card" [current_project]
set_property platform.design_intent.embedded "true" [current_project]
set_property platform.design_intent.server_managed "false" [current_project]
set_property platform.design_intent.external_host "false" [current_project]
set_property platform.design_intent.datacenter "false" [current_project]

# call implement
set cores [exec nproc]
puts "INFO: Number of available CPU cores: $cores"
launch_runs impl_1 -to_step write_bitstream -jobs $cores
wait_on_run impl_1

# generate xsa
write_hw_platform -fixed -force -include_bit ./${board_name}_top.xsa
validate_hw_platform ./${board_name}.xsa

# move and rename bitstream to final location
file copy -force ./${board_name}/${board_name}.runs/impl_1/${top_vhd_file}.bit ${board_name}_top.bit

