# VAPlayer V-Flash core
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

"""V-Flash: host-side offline Android image manipulation.

The engine opens a disk image without booting a VM, locates the ext4 system
partition (or treats a bare ``system.img`` as one), and exposes it for
inspection, extraction, and package flashing.
"""

from .engine import VFlashEngine, ImageHandle

__all__ = ["VFlashEngine", "ImageHandle"]
