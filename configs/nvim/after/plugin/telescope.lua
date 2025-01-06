require('telescope').setup({
  defaults = {
    layout_strategy = 'horizontal',
    layout_config = {
      horizontal = { width = 0.95, height = 0.95 }
      -- other layout configuration here
    },
    -- other defaults configuration here
  },
  -- other configuration values here
})
local builtin = require('telescope.builtin')

-- search files:
-- vim.keymap.set('n', '<leader>pf', builtin.find_files, { desc = "Find Files" })

-- search in git project files only:
vim.keymap.set('n', '<C-p>', builtin.git_files, { desc = "Find in Project Files" })

-- grep search in files:
-- vim.keymap.set('n', '<leader>ps', function()
-- 	builtin.grep_string({ search = vim.fn.input("Grep > ") })
-- end, { desc = "Grep search" })

-- vim.keymap.set('n', '<leader>grep', function()
-- 	builtin.grep_string({ search = vim.fn.input("Grep > ") })
-- end, { desc = "Grep Search" })

vim.keymap.set('n', '<leader>pf', builtin.find_files, { desc = 'Telescope find files' })
vim.keymap.set('n', '<leader>ff', builtin.find_files, { desc = 'Telescope find files' })

vim.keymap.set('n', '<leader>fg', builtin.live_grep, { desc = 'Telescope live grep' })
vim.keymap.set('n', '<leader>grep', builtin.live_grep, { desc = 'Telescope live grep' })

-- vim.keymap.set('n', '<leader>fb', builtin.buffers, { desc = 'Telescope buffers' })
-- vim.keymap.set('n', '<leader>fh', builtin.help_tags, { desc = 'Telescope help tags' })


vim.keymap.set('n', '<leader>ref', builtin.lsp_references, { desc = 'References under cursor' })
vim.keymap.set('n', '<leader>diag', builtin.diagnostics, { desc = 'Diagnostics' })
vim.keymap.set('n', '<leader>symb', builtin.lsp_document_symbols, { desc = 'Symbols' })
vim.keymap.set('n', '<leader>tree', builtin.treesitter, { desc = 'Find in Treesitter' })
vim.keymap.set('n', '<leader>def', builtin.lsp_definitions, { desc = 'Find in definitions under cursor' })
vim.keymap.set('n', '<leader>impl', builtin.lsp_implementations, { desc = 'Find in implementations under cursor' })

