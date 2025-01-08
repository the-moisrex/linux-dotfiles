require('Comment').setup({
    ---Add a space b/w comment and the line
    padding = true,
    ---Whether the cursor should stay at its position
    sticky = true,
    ---Lines to be ignored while (un)comment
    ignore = nil,
    ---LHS of toggle mappings in NORMAL mode
    toggler = {
        ---Line-comment toggle keymap
        line = '//',
        ---Block-comment toggle keymap
        block = '/\\',
    },
    ---LHS of operator-pending mappings in NORMAL and VISUAL mode
    opleader = {
        ---Line-comment keymap
        line = '<C-/>',
        ---Block-comment keymap
        block = '<C-\\>',
    },
    ---LHS of extra mappings
    extra = {
        ---Add comment on the line above
        -- above = 'gcO',
        ---Add comment on the line below
        -- below = 'gco',
        ---Add comment at the end of line
        -- eol = 'gcA',
    },
    ---Enable keybindings
    ---NOTE: If given `false` then the plugin won't create any mappings
    mappings = {
        ---Operator-pending mapping; `gcc` `gbc` `gc[count]{motion}` `gb[count]{motion}`
        basic = true,
        ---Extra mapping; `gco`, `gcO`, `gcA`
        extra = false,
    },
    ---Function to call before (un)comment
    pre_hook = nil,
    ---Function to call after (un)comment
    post_hook = nil,
});


-- local api = require("Comment.api");
-- local call = api.call;
-- vim.keymap.set("i", '<C-/>', call('toggle.linewise.current', 'g@$'), { expr = true, desc = 'Comment toggle current line' });
-- vim.keymap.set("n", '<C-/>', call('toggle.linewise.current', 'g@$'), { expr = true, desc = 'Comment toggle current line' });
-- vim.keymap.set("x", '<C-/>', function()
--     api.locked("toggle.linewise")(vim.fn.visualmode());
-- end, { desc = 'Comment toggle current line' });
