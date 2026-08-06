# Certificados del único Thing piloto

Esta carpeta no contiene credenciales reales.

Copia aquí los archivos descargados de AWS IoT Core y renómbralos
exactamente así:

```text
AmazonRootCA1.pem
device-certificate.pem.crt
device-private.pem.key
```

Puedes hacerlo automáticamente:

```bash
python scripts/install_certificates.py \
  --root-ca "/ruta/AmazonRootCA1.pem" \
  --certificate "/ruta/xxxxxxxx-certificate.pem.crt" \
  --private-key "/ruta/xxxxxxxx-private.pem.key"
```

El archivo `xxxxxxxx-public.pem.key` descargado desde AWS se conserva como
respaldo, pero el firmware no lo necesita para conectarse.

El certificado y la clave privada deben corresponder al mismo Thing cuyo
nombre se configura en `idf.py menuconfig`.

No compartas ni subas la clave privada a Git.
