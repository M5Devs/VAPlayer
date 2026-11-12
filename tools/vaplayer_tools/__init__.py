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

"""Host-side Android image provisioning and manipulation for VAPlayer.

The package drives the *offline* toolchain (``qemu-img``, ``debugfs``,
``squashfs``/``7z``, ``simg2img``) so an Android disk image can be prepared for
``vaplayer-core`` without booting a VM.
"""

from .errors import ToolsError, ToolNotFound, ImageError, FlashError
from .shell import require_tool, which, run

__all__ = [
    "ToolsError",
    "ToolNotFound",
    "ImageError",
    "FlashError",
    "require_tool",
    "which",
    "run",
]

__version__ = "0.1.0"
