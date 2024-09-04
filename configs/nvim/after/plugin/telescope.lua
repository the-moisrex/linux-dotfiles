local builtin = require('telescope.builtin')

-- search files:
vim.keymap.set('n', '<leader>pf', builtin.find_files, {})

-- search in git project files only:
vim.keymap.set('n', '<C-p>', builtin.git_files, {})

-- grep search in files:
vim.keymap.set('n', '<leader>ps', function()
	builtin.grep_string({ search = vim.fn.input("Grep > ") })
end)

vim.keymap.set('n', '<leader>grep', function()
	builtin.grep_string({ search = vim.fn.input("Grep > ") })
end)

