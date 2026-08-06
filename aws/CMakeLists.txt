set(AWS_CERT_DIR "${CMAKE_CURRENT_LIST_DIR}/certs")

foreach(cert_file
        "${AWS_CERT_DIR}/AmazonRootCA1.pem"
        "${AWS_CERT_DIR}/device-certificate.pem.crt"
        "${AWS_CERT_DIR}/device-private.pem.key")
    if(NOT EXISTS "${cert_file}")
        message(FATAL_ERROR
            "\nFalta el certificado requerido:\n  ${cert_file}\n"
            "Ejecuta primero:\n"
            "  python scripts/install_certificates.py "
            "--root-ca RUTA_CA --certificate RUTA_CERT --private-key RUTA_KEY\n")
    endif()
endforeach()

idf_component_register(
    SRCS "aws_iot_agent.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt json esp_event esp_timer
    EMBED_TXTFILES
        "certs/AmazonRootCA1.pem"
        "certs/device-certificate.pem.crt"
        "certs/device-private.pem.key"
)
