/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"

#include <stdio.h>
#include <string.h>

static int expect_valid_room(void)
{
    const char *json =
        "{\"room\":{\"room_number\":7,\"users\":["
        "{\"username\":\"PLAYER1\",\"slot\":\"ROM2\",\"ready\":true},"
        "{\"username\":\"PLAYER2\",\"slot\":\"ROM3\",\"ready\":false}],"
        "\"chat\":[{\"username\":\"PLAYER1\",\"message\":\"HELLO\"}],"
        "\"link_session_id\":\"session-1\",\"link_mode\":\"trade\","
        "\"game_started\":true,\"room_type\":\"link_cable\","
        "\"room_code\":\"48291\",\"creator\":true}}";
    IntegralApiRoom room;
    int has_room = 0;
    if (integral_api_parse_room_matching_response(json, &room, &has_room) != 0 ||
        !has_room || room.room_number != 7 || strcmp(room.room_code, "48291") != 0 ||
        strcmp(room.room_type, "link_cable") != 0 || !room.creator ||
        strcmp(room.user1, "PLAYER1") != 0 || strcmp(room.user2, "PLAYER2") != 0 ||
        strcmp(room.slot1, "ROM2") != 0 || strcmp(room.slot2, "ROM3") != 0 ||
        !room.ready1 || room.ready2 || !room.game_started || room.chat_count != 1 ||
        strcmp(room.chat[0], "PLAYER1: HELLO") != 0) {
        fprintf(stderr, "valid room response was not parsed correctly\n");
        return 1;
    }
    return 0;
}

static int expect_boundary_rooms(void)
{
    IntegralApiRoom room;
    int has_room = 0;
    if (integral_api_parse_room_matching_response(
            "{\"room\":{\"room_number\":64,\"users\":[],\"chat\":[],"
            "\"room_type\":\"link_cable\",\"room_code\":\"12345\"}}",
            &room, &has_room) != 0 || !has_room || room.room_number != 64) {
        fprintf(stderr, "ROOM64 Link Cable response was rejected\n");
        return 1;
    }
    if (integral_api_parse_room_matching_response(
            "{\"room\":{\"room_number\":128,\"users\":[],\"chat\":[],"
            "\"room_type\":\"n64\",\"room_code\":\"12345\"}}",
            &room, &has_room) != 0 || !has_room || room.room_number != 128) {
        fprintf(stderr, "ROOM128 N64 response was rejected\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    IntegralApiRoom room;
    int has_room = 1;
    if (expect_valid_room() != 0) return 1;
    if (expect_boundary_rooms() != 0) return 1;
    if (integral_api_parse_room_matching_response("{\"room\":null}", &room, &has_room) != 0 ||
        has_room) {
        fprintf(stderr, "null current room was not accepted\n");
        return 1;
    }
    if (integral_api_parse_room_matching_response(
            "{\"room\":{\"room_number\":1,\"users\":[],\"chat\":[],"
            "\"room_type\":\"link_cable\",\"room_code\":\"01234\"}}",
            &room, &has_room) == 0) {
        fprintf(stderr, "leading-zero room code was accepted\n");
        return 1;
    }
    if (integral_api_parse_room_matching_response(
            "{\"room\":{\"room_number\":65,\"users\":[],\"chat\":[],"
            "\"room_type\":\"link_cable\",\"room_code\":\"12345\"}}",
            &room, &has_room) == 0) {
        fprintf(stderr, "room type and number mismatch was accepted\n");
        return 1;
    }
    puts("room matching response test passed");
    return 0;
}
