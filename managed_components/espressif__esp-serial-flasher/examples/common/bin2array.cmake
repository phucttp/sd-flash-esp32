function(create_resources dir output)
    message(STATUS "Creating C resources from: ${dir}")

    # Tạo file đầu ra rỗng và ghi phần header C
    file(WRITE ${output} "#include <stdint.h>\n\n")

    # Lấy danh sách tất cả các file trong thư mục ESP*
    file(GLOB_RECURSE bin_paths "${dir}/ESP*/*")

    if(NOT bin_paths)
        message(WARNING "No binary files found in directory: ${dir}")
        return()
    endif()

    # Duyệt qua từng file nhị phân
    foreach(bin ${bin_paths})
        if(NOT EXISTS ${bin})
            message(WARNING "File not found: ${bin}")
            continue()
        endif()

        # Lấy tên tương đối của file (ví dụ ESP32_P4/bootloader.bin)
        file(RELATIVE_PATH rel_path ${dir} ${bin})

        # Chuyển các ký tự / . - \ thành dấu _
        string(REGEX REPLACE "[\\\\./-]" "_" filename "${rel_path}")

        # Đọc nội dung file ở dạng HEX
        file(READ ${bin} filedata HEX)

        # Thêm tiền tố 0x cho từng byte
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})

        # Tính MD5 bằng công cụ hệ thống
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E md5sum ${bin}
            OUTPUT_VARIABLE md5_output
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        # Lấy ra phần hash từ dòng kết quả
        string(REGEX REPLACE " .*" "" md5_hash ${md5_output})

        # Ghi dữ liệu vào file .c đầu ra
        file(APPEND ${output}
            "const uint8_t  ${filename}[] = {${filedata}};\n"
            "const uint32_t ${filename}_size = sizeof(${filename});\n"
            "const char     ${filename}_md5[] = \"${md5_hash}\";\n\n"
        )

        message(STATUS "Added resource: ${filename}")
    endforeach()

    message(STATUS "Resource file generated: ${output}")
endfunction()
