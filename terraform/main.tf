terraform {
  required_version = ">= 1.5.0"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 5.0"
    }
  }
}

provider "google" {
  project = var.project_id
  region  = var.region
}

resource "google_project_service" "run" {
  service            = "run.googleapis.com"
  disable_on_destroy = false
}

resource "google_project_service" "artifact_registry" {
  service            = "artifactregistry.googleapis.com"
  disable_on_destroy = false
}

resource "google_project_service" "cloud_build" {
  service            = "cloudbuild.googleapis.com"
  disable_on_destroy = false
}

resource "google_artifact_registry_repository" "pong" {
  location      = var.region
  repository_id = "pong-pp"
  format        = "DOCKER"

  depends_on = [google_project_service.artifact_registry]
}

resource "google_cloud_run_v2_service" "signal" {
  name     = "pong-signal"
  location = var.region

  depends_on = [google_project_service.run]

  template {
    scaling {
      max_instance_count = 1
      min_instance_count = var.min_instances
    }

    max_instance_request_concurrency = var.concurrency

    containers {
      image = "${var.region}-docker.pkg.dev/${var.project_id}/${google_artifact_registry_repository.pong.repository_id}/server:${var.image_tag}"

      ports {
        container_port = 8080
      }

      resources {
        limits = {
          cpu    = "1"
          memory = "128Mi"
        }
        cpu_idle = true
      }
    }
  }
}

resource "google_cloud_run_v2_service_iam_member" "public" {
  project  = google_cloud_run_v2_service.signal.project
  location = google_cloud_run_v2_service.signal.location
  name     = google_cloud_run_v2_service.signal.name
  role     = "roles/run.invoker"
  member   = "allUsers"
}
