local map = vim.api.nvim_set_keymap
local opts = { noremap = true, silent = true }

-- Move to previous/next
map('n', '<A-[>', '<Cmd>BufferPrevious<CR>', { noremap = true, silent = true, desc = 'Prev Tab' })
map('n', '<A-]>', '<Cmd>BufferNext<CR>', { noremap = true, silent = true, desc = 'Next Tab' })
map('n', '<A-Left>', '<Cmd>BufferPrevious<CR>', { noremap = true, silent = true, desc = 'Prev Tab' })
map('n', '<A-Right>', '<Cmd>BufferNext<CR>', { noremap = true, silent = true, desc = 'Next Tab' })

-- Re-order to previous/next
map('n', '<A-<>', '<Cmd>BufferMovePrevious<CR>', { noremap = true, silent = true, desc = 'Move Tab' })
map('n', '<A->>', '<Cmd>BufferMoveNext<CR>', { noremap = true, silent = true, desc = 'Move Tab' })

-- Goto buffer in position...
map('n', '<A-1>', '<Cmd>BufferGoto 1<CR>', { noremap = true, silent = true, desc = 'Tab 1' })
map('n', '<A-2>', '<Cmd>BufferGoto 2<CR>', { noremap = true, silent = true, desc = 'Tab 2' })
map('n', '<A-3>', '<Cmd>BufferGoto 3<CR>', { noremap = true, silent = true, desc = 'Tab 3' })
map('n', '<A-4>', '<Cmd>BufferGoto 4<CR>', { noremap = true, silent = true, desc = 'Tab 4' })
map('n', '<A-5>', '<Cmd>BufferGoto 5<CR>', { noremap = true, silent = true, desc = 'Tab 5' })
map('n', '<A-6>', '<Cmd>BufferGoto 6<CR>', { noremap = true, silent = true, desc = 'Tab 6' })
map('n', '<A-7>', '<Cmd>BufferGoto 7<CR>', { noremap = true, silent = true, desc = 'Tab 7' })
map('n', '<A-8>', '<Cmd>BufferGoto 8<CR>', { noremap = true, silent = true, desc = 'Tab 8' })
map('n', '<A-9>', '<Cmd>BufferGoto 9<CR>', { noremap = true, silent = true, desc = 'Tab 9' })
map('n', '<A-0>', '<Cmd>BufferLast<CR>', { noremap = true, silent = true, desc = 'Tab 0' })

-- Pin/unpin buffer
map('n', '<A-p>', '<Cmd>BufferPin<CR>', { noremap = true, silent = true, desc = 'Pin Tab' })

-- Goto pinned/unpinned buffer
--                 :BufferGotoPinned
--                 :BufferGotoUnpinned
-- Close buffer
map('n', '<A-c>', '<Cmd>BufferClose<CR>', { noremap = true, silent = true, desc = 'Close Tab' })
-- Wipeout buffer
--                 :BufferWipeout
-- Close commands
--                 :BufferCloseAllButCurrent
--                 :BufferCloseAllButPinned
--                 :BufferCloseAllButCurrentOrPinned
--                 :BufferCloseBuffersLeft
--                 :BufferCloseBuffersRight
-- Magic buffer-picking mode
-- map('n', '<C-p>', '<Cmd>BufferPick<CR>', opts)

-- Sort automatically by...
map('n', '<Space>bb', '<Cmd>BufferOrderByBufferNumber<CR>', opts)
map('n', '<Space>bn', '<Cmd>BufferOrderByName<CR>', opts)
map('n', '<Space>bd', '<Cmd>BufferOrderByDirectory<CR>', opts)
map('n', '<Space>bl', '<Cmd>BufferOrderByLanguage<CR>', opts)
map('n', '<Space>bw', '<Cmd>BufferOrderByWindowNumber<CR>', opts)

-- Other:
-- :BarbarEnable - enables barbar (enabled by default)
-- :BarbarDisable - very bad command, should never be used
