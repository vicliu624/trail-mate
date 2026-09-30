FROM python:3.12-slim-bookworm

ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 HOME=/var/lib/trail-mate
WORKDIR /app
COPY tools/geocaching/interop-requirements.txt tools/geocaching/bridge-requirements.txt /app/
RUN pip install --no-cache-dir -r interop-requirements.txt -r bridge-requirements.txt \
    && groupadd --gid 10001 trailmate \
    && useradd --uid 10001 --gid 10001 --no-create-home --home-dir /var/lib/trail-mate trailmate \
    && mkdir -p /var/lib/trail-mate/directory /var/lib/trail-mate/reticulum \
    && chown -R 10001:10001 /var/lib/trail-mate
COPY tools/geocaching/directory_service.py tools/geocaching/directory_store.py tools/geocaching/directory_sync.py tools/geocaching/packet_bridge.py /app/
USER 10001:10001
CMD ["python", "directory_service.py", "--help"]
