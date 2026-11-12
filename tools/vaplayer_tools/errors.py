# VAPlayer provisioning tools
#
# Copyright (C) 2026 VAPlayer contributors
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program.  If not, see <https://www.gnu.org/licenses/>.

"""Exception hierarchy shared by the VAPlayer tools."""


class ToolsError(Exception):
    """Base class for every error raised by the provisioning tools."""


class ToolNotFound(ToolsError):
    """A required external program is missing from the host."""

    def __init__(self, tool: str, hint: str = "") -> None:
        message = f"required tool not found: {tool}"
        if hint:
            message += f"\n  {hint}"
        super().__init__(message)
        self.tool = tool


class ImageError(ToolsError):
    """An image could not be read, written, or interpreted."""


class FlashError(ToolsError):
    """A flashable package could not be applied to an image."""
