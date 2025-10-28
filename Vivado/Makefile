board_name := pynq_z1

all: create_project bitstream check_timing clean_logs
	@echo
	@tput setaf 2 ; echo "Built $(overlay_name) successfully!"; tput sgr0;
	@echo

create_project:
	vivado -mode batch -nojournal -source scripts/create_project.tcl -notrace
	
bitstream:
	vivado -mode batch -nojournal -source scripts/build_bitstream.tcl -notrace

check_timing:
	vivado -mode batch -nojournal -source check_timing.tcl -notrace

clean_logs:
	rm -rf *.jou *.log *.str NA .Xil
	
clean:
	rm -rf $(board_name) *.jou *.log *.str NA .Xil
