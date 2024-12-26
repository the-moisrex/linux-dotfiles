# I know there are memory leaks in this script, I just don't care

set breakpoint pending on
set $__bplist_len = 0
set $__bplist = (int**) 0

define __resize_array
  if $__bplist_len < $arg0
    set $__bplist_newlen = $arg0 * 2
    printf "Resizing array from %d to %d\n", $__bplist_len, $__bplist_newlen
    set $__bplist_new = (int**) malloc($__bplist_newlen * sizeof(int))
    set $__bplist_index = 0
    while $__bplist_index < $__bplist_len
      set $__bplist_new[$__bplist_index] = $__bplist[$__bplist_index]
      set $__bplist_index = $__bplist_index + 1
    end
    set $__bplist_len = $__bplist_newlen
    
    # Fill with zeros
    while $__bplist_index < $__bplist_len
      set $__bplist_new[$__bplist_index] = 0
      set $__bplist_index = $__bplist_index + 1
    end
    set $__bplist = $__bplist_new
  end
end


define __enable_breakpoints_of
  # arg0 = breakpoint id

  set $__bps_size = $_strlen((char*)$__bplist[$arg0])
  set $__bps_len = (int) ($__bps_size / sizeof(int))
  set $__cur = 0
  while $__cur < $__bps_len
    set $__bp = $__bplist[$arg0][$__cur]
    printf "  Enabling breakpoint: %d\n", $__bp
    enable once $__bp
    $__cur = $__cur + 1
  end
end

define __add_bp_to_batch
  # arg0 = breakpoint id of the dependent breakpoint
  # arg1 = breakpoint id of non-dependent breakpoint
  __resize_array $arg0

  # this is not the length, but the size in bytes:
  if (int*) $__bplist[$arg0] != (int*) 0
    set $__bps_size = $_strlen((char*)$__bplist[$arg0])
  else
    set $__bps_size = 0
  end
  set $__bps_len = (int) ($__bps_size / sizeof(int))
  set $___ptr = (int*) malloc($__bps_size + (sizeof(int) * 2))

  # copy the old batch ids
  set $__bp_index = 0
  while $__bp_index != $__bps_len
    set $___ptr[$__bp_index] = $__bplist[$arg1][$__bp_index]
    set $__bp_index = $__bp_index + 1
  end

  set $___ptr[$__bp_index] = $arg1
  set $__bp_index = $__bp_index + 1
  # set the ending zero to make strlen work
  set $___ptr[$__bp_index] = 0

  printf "  Breakpoint[%d] += breakpoint #%d\n", $arg0, $arg1
  set $__bplist[$arg0] = $___ptr
end

define __reset_breakpoint_start
  set $__bp_index = 0
  while $__bp_index != $__bplist_len
    set $__cur = $__bplist[$__bp_index]
    if $__cur != (int*) 0
      printf "  Enabling breakpoint:  %d\n", $__bp_index
      enable once $__bp_index
    end
    set $__bp_index = $__bp_index + 1
  end
  set $__bp_index = 0
  while $__bp_index != $__bplist_len
    set $__cur = $__bplist[$__bp_index]
    if $__cur != (int*) 0
      while *$__cur != 0
        printf "  Disabling breakpoint:  %d\n", *$__cur
        set $__bp = *$__cur
        disable $__bp
        set $__cur = $__cur + 1
      end
    end
    set $__bp_index = $__bp_index + 1
  end
end

define __chain_breakpoint
  printf "Chaining breakpoint %d after breakpoint %d\n", $arg0, $arg1

  __add_bp_to_batch $arg0 $arg1

  commands $arg0
      __enable_breakpoints_of $_hit_bpnum
  end

  __reset_breakpoint_start

  printf "\n"
end

define __rchain_breakpoint
  printf "Chaining breakpoint range %d-%d to %d:\n", $arg0, $arg1, $arg3
  set $__cur = $arg0
  while $__cur <= $arg1
    __add_bp_to_batch $__cur $arg3

    commands $__cur
      __enable_breakpoints_of $_hit_bpnum
    end

    set $__cur = $__cur + 1
  end

  __reset_breakpoint_start

  printf "\n"
end

define chain_up
  __chain_breakpoint $bpnum ($bpnum - 1)
end
document chain_up
Chain the last two breakpoints together
end

define chain
  set $__prev_bpnum = $bpnum
  break $arg0
  __chain_breakpoint $bpnum $__prev_bpnum
end
document chain
Use "break" but chain them together so they're hit only if
the last batch of the breakpoints have been hit.
end

define rchain
  set $__last_bpnum = $bpnum ? $bpnum : 0
  set $__cur_bpnum_start = $bpnum + 1
  rbreak $arg0
  __rchain_breakpoint $__last_bpnum_start $bpnum $__last_bpnum
end
document rchain
Same as "chain" but for "rbreak"
end

# resets the chain to the current breakpoint
define chain_reset
  set $__bpstart = $bpnum ? $bpnum : 0
  if $__bpstart > 0
    # clear the `commands` of the last breakpoint
    commands $__bpstart
    end
  end
end

define rechain
  __chain_breakpoint $arg0 $arg1
end
document rechain
Chain two breakpoints together.
Usage: rechain 1 [2]
if you specify only one breakpoint, the last breakpoint before that is chosen.
end


chain_reset
