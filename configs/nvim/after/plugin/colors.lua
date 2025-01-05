local color = "carbonfox"

function ColorMyPencils(color)
	color = color or "rose-pine"
	local status, err = pcall(function()
        vim.cmd.colorscheme(color)
    end)

    if not status then
        print("Invalid colorscheme '" .. color .. "'")
    end

	-- transparent background:
	-- vim.api.nvim_set_hl(0, "Normal", { bg = "none" })
	-- vim.api.nvim_set_hl(0, "NormalFloat", { bg = "none" })
end

ColorMyPencils(color)


local themes = {
    color,
    "terafox",
    "tokyonight-night",
    "carbonfox",
    "duskfox",
    "nightfox",
    "catppuccin-mocha",
    "gruvbox-material",
    "rose-pine",
    "dayfox",
    -- "everforest",
}
local current_theme_index = 1

function change_theme()
    current_theme_index = (current_theme_index % #themes) + 1
    vim.cmd("colorscheme " .. themes[current_theme_index])
    print("Switched to theme: " .. themes[current_theme_index])
end

vim.keymap.set('n', '<leader>t', ':lua change_theme()<CR>', { noremap = true, silent = true })

