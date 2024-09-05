
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

ColorMyPencils()
