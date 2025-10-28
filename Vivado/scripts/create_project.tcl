set project_name pynq_z1
set part xc7z020clg400-1 

if {![info exists vivado_dir]}   {set vivado_dir [file join ${project_name}] }


create_project ${project_name} ${vivado_dir} -part ${part}

set proj_dir [get_property directory [current_project]]

puts "INFO: Adding custom IP repository..."
set_property  ip_repo_paths  custom_ips/ [current_project]
update_ip_catalog

set_property "default_lib"        "xil_defaultlib" [current_project]
set_property "part"               "${part}"        [current_project]
set_property "simulator_language" "Mixed"          [current_project]
set_property "target_language"    "VHDL"           [current_project]

if {[string equal [get_filesets -quiet sources_1] ""]} {
    create_fileset -srcset sources_1
}
if {[string equal [get_filesets -quiet constrs_1] ""]} {
    create_fileset -srcset constrs_1
}

# add source and constraints to corresponding fileset
add_files -norecurse -fileset sources_1 [glob -type f -directory src *.{vhd,edn}]
add_files -norecurse -fileset constrs_1 [glob -type f -directory src *.{tcl,xdc}]

source scripts/${project_name}_bd.tcl

