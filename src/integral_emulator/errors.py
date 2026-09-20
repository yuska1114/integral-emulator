# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Domain exceptions for INTEGRAL EMULATOR."""


class LeagueError(Exception):
    """Base class for expected domain failures."""


class DuplicateUserError(LeagueError):
    """Raised when a username is already registered."""


class AuthenticationError(LeagueError):
    """Raised when login or token validation fails."""


class ClientVersionNotAllowedError(LeagueError):
    """Compatibility guidance, not an authentication/security boundary."""


class RegistrationDisabledError(LeagueError):
    """Raised when public self-registration is disabled."""


class NotFoundError(LeagueError):
    """Raised when a requested entity does not exist."""


class RevisionConflictError(LeagueError):
    """Raised when a client tries to commit from a stale save revision."""


class SaveLockedError(LeagueError):
    """Raised when a save is locked by another owner."""


class ValidationError(LeagueError):
    """Raised when uploaded data or metadata is invalid."""


class MobileCreateAuthSessionConflictError(ValidationError):
    """Raised when a Mobile create replay belongs to another login session."""


class MobileCreateAbortedError(ValidationError):
    """Raised when an incomplete Mobile create was durably aborted."""


class RoomMatchingError(LeagueError):
    """Base class for stable ROOM matching API failures."""


class InvalidRoomModeError(RoomMatchingError):
    pass


class RoomPoolFullError(RoomMatchingError):
    pass


class InvalidRoomCodeError(RoomMatchingError):
    pass


class RoomCodeUnavailableError(RoomMatchingError):
    pass


class AlreadyInRoomError(RoomMatchingError):
    pass


class RoomJoinRateLimitedError(RoomMatchingError):
    pass


class GameSessionFenceError(ValidationError):
    """Raised when a game operation carries missing or stale fence credentials."""


class GameSessionExpiredError(ValidationError):
    """Raised when a fenced operation targets an expired game session."""
