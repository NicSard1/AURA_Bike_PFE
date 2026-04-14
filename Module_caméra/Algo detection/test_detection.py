import cv2
from ultralytics import YOLO

# Charger un modèle YOLO pré-entraîné
model = YOLO("yolov8n.pt")

# Ouvrir la vidéo
cap = cv2.VideoCapture("video.mp4")

if not cap.isOpened():
    print("Erreur : impossible d'ouvrir la vidéo.")
    exit()

while True:
    ret, frame = cap.read()
    if not ret:
        break

    # Détection sur l'image
    results = model(frame)

    # Dessiner les résultats directement sur l'image
    annotated_frame = results[0].plot()

    # Afficher la vidéo annotée
    cv2.imshow("Aura Bike - Detection", annotated_frame)

    # Touche q pour quitter
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()