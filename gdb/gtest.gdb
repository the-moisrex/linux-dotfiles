python
import subprocess
import gdb

def set_gtest_breakpoint(test_name, break_cmd):
    """Runs gtest-finder and sets the specified type of breakpoint."""
    if not test_name:
        print("Please provide a test name.")
        return
        
    try:
        location = subprocess.check_output(["gtest-finder", test_name]).decode('utf-8').strip()
        if location:
            gdb.execute(f"{break_cmd} {location}")
        else:
            print(f"No location found for {test_name}")
    except Exception as e:
        print(f"Failed to run gtest-finder: {e}")


class GtestBreakCommand(gdb.Command):
    """Find a Google Test and set a breakpoint.
    The type of breakpoint depends on the command used."""
    
    def __init__(self, cmd_name, break_cmd):
        super(GtestBreakCommand, self).__init__(cmd_name, gdb.COMMAND_BREAKPOINTS)
        self.break_cmd = break_cmd

    def invoke(self, arg, from_tty):
        set_gtest_breakpoint(arg.strip(), self.break_cmd)

# Register the generalized commands
GtestBreakCommand("gtest-break",   "break")     # Standard breakpoint
GtestBreakCommand("gtest-tbreak",  "tbreak")    # Temporary breakpoint
GtestBreakCommand("gtest-hbreak",  "hbreak")    # Hardware breakpoint
GtestBreakCommand("gtest-thbreak", "thbreak")   # Temporary hardware breakpoint
end

