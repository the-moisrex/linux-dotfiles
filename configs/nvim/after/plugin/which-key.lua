local whichkey = require("which-key");
whichkey.setup({
    delay = 1000
});

vim.keymap.set('n', '<leader>?', function()
    whichkey.show({ global = true });
end);


